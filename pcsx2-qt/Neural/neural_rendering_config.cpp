// SPDX-License-Identifier: GPL-3.0-or-later
#include "neural_rendering_config.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QMutex>
#include <QMutexLocker>
#include <QStringList>
#include <cstdlib>

namespace neural_rendering
{
	namespace
	{
		bool startup_ready = false;
		QMutex startup_mutex;
		QString startup_status = QStringLiteral("Integración aún no inicializada.");

		bool atomic_write(const QString& path, const QByteArray& bytes, QString* error)
		{
			QSaveFile file(path);
			if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit())
			{
				if (error)
					*error = path + ": " + file.errorString();
				return false;
			}
			return true;
		}

		QStringList lines_of(const QString& text)
		{
			QString normalized = text;
			normalized.replace("\r\n", "\n");
			if (normalized.startsWith(QChar(0xfeff)))
				normalized.remove(0, 1);
			return normalized.split('\n');
		}

		bool section_line(const QString& line)
		{
			return line.startsWith('[');
		}

		QString section_name(const QString& line)
		{
			return line.mid(1).section(']', 0, 0).trimmed();
		}

		bool key_line(const QString& line, const QString& key)
		{
			return !line.startsWith(';') && !line.startsWith('#') && !line.startsWith('/') && line.contains('=') && line.section('=', 0, 0).trimmed() == key;
		}

		bool set_environment(const char* name, const QString& value)
		{
#ifdef _WIN32
			// Keep the wide CRT environment and GetEnvironmentVariableW in sync. qputenv
			// uses the narrow CRT API on Windows and corrupts non-ASCII portable paths.
			return _wputenv_s(QString::fromLatin1(name).toStdWString().c_str(), value.toStdWString().c_str()) == 0;
#else
			return value.isEmpty() ? qunsetenv(name) : qputenv(name, value.toUtf8());
#endif
		}

		bool prepend_environment_list(const char* name, const QStringList& entries)
		{
			QStringList values = qEnvironmentVariable(name).split(';', Qt::SkipEmptyParts);
			for (auto it = entries.crbegin(); it != entries.crend(); ++it)
			{
				values.removeAll(*it);
				values.prepend(*it);
			}
			return set_environment(name, values.join(';'));
		}

		bool disable_process_layers()
		{
			// Set both flags even if one update fails; never leave a partially enabled kit.
			const bool reshade_disabled = set_environment("DISABLE_VK_LAYER_reshade_1", "1");
			const bool local_reshade_disabled = set_environment("DISABLE_VK_LAYER_PCSX2_reshade", "1");
			const bool feeder_disabled = set_environment("DISABLE_VK_LAYER_feed_vk", "1");
			QStringList previous_layers = qEnvironmentVariable("VK_INSTANCE_LAYERS").split(';', Qt::SkipEmptyParts);
			previous_layers.removeAll("VK_LAYER_reshade");
			previous_layers.removeAll("VK_LAYER_PCSX2_reshade");
			previous_layers.removeAll("VK_LAYER_feed_vk");
			const bool list_updated = set_environment("VK_INSTANCE_LAYERS", previous_layers.join(';'));
			return reshade_disabled && local_reshade_disabled && feeder_disabled && list_updated;
		}

		QString validate_layer(const QDir& root, const QString& manifest_name, const QString& layer_name,
			const QString& library_name, const QString& disable_flag)
		{
			QFile manifest(root.filePath(manifest_name));
			if (!manifest.open(QIODevice::ReadOnly))
				return "No se puede leer el manifiesto Vulkan: " + manifest_name;
			QJsonParseError parse_error;
			const QJsonDocument document = QJsonDocument::fromJson(manifest.readAll(), &parse_error);
			const QJsonObject layer = document.object().value("layer").toObject();
			if (parse_error.error != QJsonParseError::NoError || layer.value("name").toString() != layer_name)
				return "El manifiesto Vulkan no es válido: " + manifest_name;
			const QString library_path = layer.value("library_path").toString();
			if (library_path.contains('/'))
				return "El cargador Vulkan de Windows requiere separadores de ruta \\ en el manifiesto: " + manifest_name;
			const QFileInfo library(QDir(QFileInfo(manifest).absolutePath()).filePath(library_path));
			if (library_path.isEmpty() || library.canonicalFilePath() != QFileInfo(root.filePath(library_name)).canonicalFilePath())
				return "El manifiesto Vulkan no apunta al componente de este paquete: " + library_name;
			if (layer.value("disable_environment").toObject().value(disable_flag).toString() != "1")
				return "El manifiesto Vulkan no permite desactivar esta capa para el proceso: " + manifest_name;
			return {};
		}
	} // namespace

	QString root_path()
	{
		return QCoreApplication::applicationDirPath();
	}

	QString config_path()
	{
		return QDir(root_path()).filePath("ReShade.ini");
	}

	QString preset_path()
	{
		return QDir(root_path()).filePath("ReShadePreset.ini");
	}

	QString read_text(const QString& path, QString* error)
	{
		if (error)
			error->clear();
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly))
		{
			if (error)
				*error = path + ": " + file.errorString();
			return {};
		}
		const QByteArray data = file.readAll();
		if (file.error() != QFileDevice::NoError && error)
			*error = path + ": " + file.errorString();
		return QString::fromUtf8(data);
	}

	bool write_text(const QString& path, const QString& text, QString* error)
	{
		if (error)
			error->clear();
		if (QFileInfo::exists(path))
		{
			QFile original(path);
			if (!original.open(QIODevice::ReadOnly))
			{
				if (error)
					*error = path + ": " + original.errorString();
				return false;
			}
			const QByteArray previous = original.readAll();
			if (original.error() != QFileDevice::NoError)
			{
				if (error)
					*error = path + ": " + original.errorString();
				return false;
			}
			if (previous == text.toUtf8())
				return true;
			if (!atomic_write(path + ".pcsx2-backup", previous, error))
				return false;
		}
		return atomic_write(path, text.toUtf8(), error);
	}

	QString ini_value(const QString& text, const QString& section, const QString& key, const QString& fallback)
	{
		QString current;
		QString value = fallback;
		bool found = false;
		for (const QString& raw : lines_of(text))
		{
			const QString line = raw.trimmed();
			if (section_line(line))
				current = section_name(line);
			else if (current == section && key_line(line, key))
			{
				const QString part = line.mid(line.indexOf('=') + 1).trimmed();
				if (!found)
					value = part;
				else if (!part.isEmpty())
					value += (value.isEmpty() ? QString() : QStringLiteral(",")) + part;
				found = true;
			}
		}
		return value;
	}

	void set_ini_value(QString& text, const QString& section, const QString& key, const QString& value)
	{
		QStringList lines = lines_of(text);
		QString current;
		bool found = false;
		bool in_section = section.isEmpty();
		qsizetype insertion = section.isEmpty() ? 0 : -1;
		for (qsizetype i = 0; i < lines.size(); ++i)
		{
			const QString line = lines[i].trimmed();
			if (section_line(line))
			{
				current = section_name(line);
				in_section = current == section;
				if (in_section)
					insertion = i + 1;
			}
			else if (in_section)
			{
				if (key_line(line, key))
				{
					if (!found)
					{
						lines[i] = key + '=' + value;
						found = true;
					}
					else
						lines.removeAt(i--);
				}
				insertion = i + 1;
			}
		}
		if (!found)
		{
			if (insertion < 0)
			{
				if (!lines.isEmpty() && !lines.last().isEmpty())
					lines.append(QString());
				lines.append('[' + section + ']');
				lines.append(key + '=' + value);
			}
			else
				lines.insert(insertion, key + '=' + value);
		}
		text = lines.join('\n');
		if (!text.endsWith('\n'))
			text += '\n';
	}

	QString default_config()
	{
		return QStringLiteral(
			"[GENERAL]\n"
			"EffectSearchPaths=.\\reshade-shaders\\Shaders\\**\n"
			"TextureSearchPaths=.\\reshade-shaders\\Textures\\**\n"
			"PresetPath=.\\ReShadePreset.ini\n"
			"PreprocessorDefinitions=DLSS5_MV_PROVIDER=3\n"
			"PerformanceMode=0\n\n"
			"[ADDON]\nAddonPath=.\\\n\n"
			"[INPUT]\nKeyOverlay=0,0,0,0\nKeyEffects=0,0,0,0\n\n"
			"[OVERLAY]\nTutorialProgress=4\n\n"
			"[SCREENSHOT]\nSavePath=.\\screenshots\n\n"
			"[RenoDX.DLSS5]\nNeuralUplift=1\nNREnableUpscaling=0\nEnableHooks=2\nNRStyle=1\nNRPaperWhiteScale=15.401\nNRIntensity=1\nNRTransferStrength=1\nNRColorStrength=1\n");
	}

	QString default_preset()
	{
		return QStringLiteral(
			"Techniques=Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx\n"
			"TechniqueSorting=Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@DLSS5_Feed.fx\n"
			"PreprocessorDefinitions=DLSS5_MV_PROVIDER=3\n");
	}

	QString settings_only_config(QString text)
	{
		set_ini_value(text, "INPUT", "KeyOverlay", "0,0,0,0");
		set_ini_value(text, "INPUT", "KeyEffects", "0,0,0,0");
		set_ini_value(text, "OVERLAY", "TutorialProgress", "4");
		set_ini_value(text, "ADDON", "AddonPath", ".\\");
		// ReShade list values escape a literal comma by doubling it.
		const QString disabled = ini_value(text, "ADDON", "DisabledAddons");
		QStringList entries;
		QString entry;
		for (qsizetype i = 0; i <= disabled.size(); ++i)
		{
			if (i < disabled.size() && disabled[i] != ',')
				entry += disabled[i];
			else if (i + 1 < disabled.size() && disabled[i + 1] == ',')
			{
				entry += ',';
				++i;
			}
			else
			{
				if (!entry.isEmpty() && entry != "PCSX2 Settings Only" &&
					entry.section('@', -1).compare("pcsx2-settings-only.addon64", Qt::CaseInsensitive) != 0)
					entries.append(entry.replace(",", ",,"));
				entry.clear();
			}
		}
		set_ini_value(text, "ADDON", "DisabledAddons", entries.join(','));
		return text;
	}

	bool enabled()
	{
		QFile file(QDir(root_path()).filePath("neural-rendering.json"));
		if (!file.open(QIODevice::ReadOnly))
			return false;
		return QJsonDocument::fromJson(file.readAll()).object().value("enabled").toBool(false);
	}

	bool save_enabled(bool value, QString* error)
	{
		if (error)
			error->clear();
		if (value)
		{
			const QString problem = validate_runtime();
			if (!problem.isEmpty())
			{
				if (error)
					*error = problem;
				return false;
			}
		}
		const QJsonObject settings{{"schema", 1}, {"enabled", value}};
		return atomic_write(QDir(root_path()).filePath("neural-rendering.json"), QJsonDocument(settings).toJson(), error);
	}

	QString validate_runtime(const QString& directory)
	{
#ifndef _WIN32
		Q_UNUSED(directory);
		return QStringLiteral("Esta integración experimental requiere Windows x64 y Vulkan.");
#else
		QStringList missing;
		const QDir root(directory);
		for (const QString& name : {
				 QStringLiteral("ReShade64.dll"), QStringLiteral("neural-rendering/ReShade64.json"),
				 QStringLiteral("neural-rendering/VkLayer_feed_vk.dll"), QStringLiteral("neural-rendering/VkLayer_feed_vk.json"),
				 QStringLiteral("dlss5-feed.addon64"), QStringLiteral("renodx-dlss5.addon64"),
				 QStringLiteral("pcsx2-settings-only.addon64"),
				 QStringLiteral("nvngx_dlssnr.dll"), QStringLiteral("nvngx_dlss.dll"),
				 QStringLiteral("reshade-shaders/Shaders/ReShade.fxh"),
				 QStringLiteral("reshade-shaders/Shaders/DLSS5_Feed.fx"),
				 QStringLiteral("reshade-shaders/Shaders/lumenite_Kernel.fx")})
		{
			const QFileInfo file(root.filePath(name));
			if (!file.isFile() || file.size() == 0)
				missing.append(name);
		}
		if (!missing.isEmpty())
			return "Faltan componentes del paquete:\n" + missing.join('\n');
		const QString reshade_error = validate_layer(root, "neural-rendering/ReShade64.json", "VK_LAYER_PCSX2_reshade",
			"ReShade64.dll", "DISABLE_VK_LAYER_PCSX2_reshade");
		if (!reshade_error.isEmpty())
			return reshade_error;
		return validate_layer(root, "neural-rendering/VkLayer_feed_vk.json", "VK_LAYER_feed_vk",
			"neural-rendering/VkLayer_feed_vk.dll", "DISABLE_VK_LAYER_feed_vk");
#endif
	}

	bool prepared()
	{
		QMutexLocker lock(&startup_mutex);
		return startup_ready;
	}

	QString initialize()
	{
		QMutexLocker lock(&startup_mutex);
		startup_ready = false;
#ifndef _WIN32
		return startup_status = "La integración neural de este paquete requiere Windows x64.";
#else
		// Vulkan uses explicit layers. Hooking Feeder's private D3D12 device as well
		// creates an extra ReShade device reference and prevents safe add-on teardown.
		// No ReShade input/network hooks are needed for this settings-only integration.
		if (!set_environment("RESHADE_DISABLE_GRAPHICS_HOOK", "1") ||
			!set_environment("RESHADE_DISABLE_INPUT_HOOK", "1") ||
			!set_environment("RESHADE_DISABLE_NETWORK_HOOK", "1"))
			return startup_status = "No se pudo aislar la integración Vulkan.";
		// A system-wide implicit ReShade layer also notices our ReShade.ini. Explicitly
		// disable it for this process until the portable runtime has been validated.
		if (!disable_process_layers())
			return startup_status = "No se pudo preparar el entorno Vulkan del proceso.";
		if (!enabled())
		{
			startup_ready = true;
			return startup_status = "Integración neural desactivada.";
		}
		const QString error = validate_runtime();
		if (!error.isEmpty())
			return startup_status = "No se ha cargado la integración:\n" + error;
		if (!QFileInfo::exists(config_path()))
			return startup_status = "No se ha cargado la integración: falta ReShade.ini.";
		QString read_error;
		const QString config = read_text(config_path(), &read_error);
		const QString base_override = ini_value(config, "INSTALL", "BasePath");
		if (!read_error.isEmpty())
			return startup_status = read_error;
		if (!base_override.isEmpty() && QFileInfo(QDir(root_path()).absoluteFilePath(base_override)).canonicalFilePath() != QFileInfo(root_path()).canonicalFilePath())
			return startup_status = "No se ha cargado la integración: INSTALL / BasePath redirige ReShade a otra carpeta. Retira esa clave para usar estos ajustes.";
		if (!write_text(config_path(), settings_only_config(config), &read_error))
			return startup_status = "No se ha cargado la integración: no se pudo bloquear el menú de ReShade.\n" + read_error;

		const QString layer_path = QDir::toNativeSeparators(QDir(root_path()).filePath("neural-rendering"));
		// ADD preserves layers installed by drivers and developer tools.
		// The loader ignores ADD_LAYER_PATH when the overriding LAYER_PATH is set.
		const char* path_variable = qEnvironmentVariableIsSet("VK_LAYER_PATH") ? "VK_LAYER_PATH" : "VK_ADD_LAYER_PATH";
		if (!prepend_environment_list(path_variable, {layer_path}) ||
			!set_environment("RESHADE_BASE_PATH_OVERRIDE", QDir::toNativeSeparators(root_path())) ||
			!prepend_environment_list("VK_INSTANCE_LAYERS", {"VK_LAYER_PCSX2_reshade", "VK_LAYER_feed_vk"}) ||
			!set_environment("DISABLE_VK_LAYER_PCSX2_reshade", {}) ||
			!set_environment("DISABLE_VK_LAYER_feed_vk", {}))
		{
			disable_process_layers();
			return startup_status = "No se pudo configurar el entorno de ReShade y Feeder.";
		}
		startup_ready = true;
		return startup_status = "Capas Vulkan de ReShade y Feeder solicitadas para este proceso. La evaluación neural se confirma en los registros al ejecutar un juego.";
#endif
	}

	QString diagnostics()
	{
		QMutexLocker lock(&startup_mutex);
		QString result = "PCSX2 Neural Rendering — experimental\n";
		result += "Carpeta: " + root_path() + '\n';
		result += QString("Activación guardada: %1 (sin reiniciar PCSX2)\n").arg(enabled() ? "sí" : "no");
		result += "Estado de inicio: " + startup_status + "\n\n";
		const QString problem = validate_runtime();
		result += problem.isEmpty() ? "Componentes presentes. Esto no confirma que DLSS esté evaluando fotogramas.\n" : problem + '\n';
		result += "\nMenú ReShade bloqueado dentro del juego (teclado y mando). Configura los efectos desde Ajustes. Renderizador: Vulkan con GPU NVIDIA.\n";
		result += "Aplicar conserva la partida y recrea el motor gráfico, sin reiniciar PCSX2. La configuración es global a este paquete.\n";
		for (const QString& name : {QStringLiteral("ReShade.log"), QStringLiteral("dlss5-feed.log")})
		{
			QFile log(QDir(root_path()).filePath(name));
			if (!log.open(QIODevice::ReadOnly))
				continue;
			if (log.size() > 12000)
				log.seek(log.size() - 12000);
			result += "\n--- " + name + " (final del archivo; puede ser de una sesión anterior) ---\n" + QString::fromUtf8(log.readAll());
		}
		return result;
	}
} // namespace neural_rendering
