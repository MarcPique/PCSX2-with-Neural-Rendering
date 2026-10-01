// SPDX-License-Identifier: GPL-3.0-or-later
#include "NeuralRenderingController.h"
#include "neural_rendering_config.h"
#include "QtHost.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/GS/GS.h"
#include "pcsx2/GS/Renderers/Common/GSDevice.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <atomic>
#include <array>

namespace NeuralRendering
{
	static std::atomic_bool s_applying{false};

	bool IsApplying() { return s_applying.load(); }

	Configuration Load()
	{
		const auto read = [](const QString& path, const QString& fallback) {
			return QFileInfo::exists(path) ? neural_rendering::read_text(path) : fallback;
		};
		return {read(neural_rendering::config_path(), neural_rendering::default_config()),
			read(neural_rendering::preset_path(), neural_rendering::default_preset()),
			read(QDir(neural_rendering::root_path()).filePath("dlss5-feed.cfg"),
				"enabled=1\nmode=2\nhdr=1\ndepth_inverted=1\nmv_scale_x=1\nmv_scale_y=1\n"),
			neural_rendering::enabled()};
	}

	void Apply(Configuration configuration, std::function<void(QString)> completion)
	{
		if (s_applying.exchange(true))
		{
			completion(QStringLiteral("Ya se está aplicando otra configuración neural."));
			return;
		}
		const auto finish = [completion = std::move(completion)](QString error) {
			QMetaObject::invokeMethod(qApp, [completion, error = std::move(error)]() {
			s_applying.store(false);
			completion(error); }, Qt::QueuedConnection);
		};
		if (configuration.enabled)
		{
			const QString error = neural_rendering::validate_runtime();
			if (!error.isEmpty())
			{
				finish(error);
				return;
			}
		}
		configuration.config = neural_rendering::settings_only_config(configuration.config);
		// The CPU thread serializes this with game startup/shutdown and other settings.
		QMetaObject::invokeMethod(g_emu_thread, [configuration = std::move(configuration), finish]() {
		auto work = [configuration, finish]() {
			if (MTGS::IsOpen() && configuration.enabled &&
				(!g_gs_device || g_gs_device->GetRenderAPI() != RenderAPI::Vulkan))
			{
				finish(QStringLiteral("Selecciona Vulkan en Ajustes → Gráficos antes de activar Neural / ReShade."));
				return;
			}
			QString error;
			struct Previous { QString path; QString text; bool existed; };
			std::array<Previous, 4> previous;
			bool captured = false;
			const auto rollback = [&]() {
				if (!captured) return;
				for (const auto& file : previous)
				{
					QString problem;
					if (file.existed)
					{
						if (!neural_rendering::write_text(file.path, file.text, &problem)) error += '\n' + problem;
					}
					else if (QFileInfo::exists(file.path) && !QFile::remove(file.path))
						error += "\nNo se pudo retirar " + file.path;
				}
				neural_rendering::initialize();
			};
			const auto commit = [&]() {
				const QDir root(neural_rendering::root_path());
				previous = {{{neural_rendering::config_path(), {}, false},
					{neural_rendering::preset_path(), {}, false},
					{root.filePath("dlss5-feed.cfg"), {}, false},
					{root.filePath("neural-rendering.json"), {}, false}}};
				for (auto& file : previous)
				{
					file.existed = QFileInfo::exists(file.path);
					if (file.existed) file.text = neural_rendering::read_text(file.path, &error);
					if (!error.isEmpty()) return;
				}
				captured = true;
				if (!neural_rendering::write_text(previous[0].path, configuration.config, &error) ||
					!neural_rendering::write_text(previous[1].path, configuration.preset, &error) ||
					!neural_rendering::write_text(previous[2].path, configuration.feeder, &error) ||
					!neural_rendering::save_enabled(configuration.enabled, &error))
				{
					rollback();
					return;
				}
				const QString status = neural_rendering::initialize();
				if (!neural_rendering::prepared())
				{
					error = status;
					rollback();
				}
			};
			if (MTGS::IsOpen())
			{
				// GSreopen freezes/restores GS state. This does not reboot the VM.
				// Needs proper testing across game-specific framebuffer/readback hacks.
				auto old_config = GSConfig;
				old_config.Renderer = GSGetCurrentRenderer();
				const auto failed = [&]() {
					error = QStringLiteral("No se pudo abrir el motor gráfico con estos efectos. Se restauró la configuración anterior.");
					rollback();
				};
				if (!GSreopen(true, true, old_config.Renderer, &old_config, commit, failed) && error.isEmpty())
					error = QStringLiteral("No se pudo recrear el motor gráfico. Consulta el registro de PCSX2.");
			}
			else commit();
			finish(error);
		};
		if (MTGS::IsOpen())
		{
			MTGS::RunOnGSThread(std::move(work));
			// Match MTGS::ApplySettings: unsynchronized readbacks may otherwise
			// access the renderer while it is being replaced on the GS thread.
			if (EmuConfig.GS.HWDownloadMode == GSHardwareDownloadMode::Unsynchronized)
				MTGS::WaitGS(false, false, false);
		}
		else work(); }, Qt::QueuedConnection);
	}
} // namespace NeuralRendering
