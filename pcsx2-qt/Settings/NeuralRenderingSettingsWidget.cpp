// SPDX-License-Identifier: GPL-3.0-or-later
#include "NeuralRenderingSettingsWidget.h"
#include "Neural/neural_rendering_config.h"
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProcess>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSlider>
#include <QTabWidget>
#include <QVBoxLayout>
#include <algorithm>
#include <cmath>

NeuralRenderingSettingsWidget::NeuralRenderingSettingsWidget(std::function<void()> select_vulkan, QWidget* parent)
	: QWidget(parent)
	, m_configuration(NeuralRendering::Load())
{
	auto* layout = new QVBoxLayout(this);
	auto* description = new QLabel(tr("Neural / ReShade para Vulkan. Todos los controles están aquí; Home y el menú de ReShade dentro del juego están bloqueados. Aplicar conserva la partida y recrea el motor gráfico: puede haber una pausa breve, sin reiniciar PCSX2."), this);
	description->setWordWrap(true);
	layout->addWidget(description);
	m_enabled = new QCheckBox(tr("Activar Neural / ReShade"), this);
	m_enabled->setObjectName("neural_enabled");
	layout->addWidget(m_enabled);
	connect(m_enabled, &QCheckBox::clicked, this, [this](bool enabled) {
		m_configuration.enabled = enabled;
		if (enabled && !neural_rendering::validate_runtime().isEmpty())
			download(true);
		else
			apply();
	});
	auto* tabs = new QTabWidget(this);
	layout->addWidget(tabs, 1);
	const auto page = [tabs](const QString& name) {
		auto* scroll = new QScrollArea(tabs);
		scroll->setWidgetResizable(true);
		auto* content = new QWidget(scroll);
		auto* form = new QFormLayout(content);
		form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
		scroll->setWidget(content);
		tabs->addTab(scroll, name);
		return form;
	};
	auto* neural = page(tr("Neural"));
	auto* presets = new QWidget(this);
	auto* preset_row = new QHBoxLayout(presets);
	preset_row->setContentsMargins(0, 0, 0, 0);
	struct Profile
	{
		const char* name;
		double intensity, tone, structure, skin;
		int style;
	};
	for (const Profile p : {Profile{"Suave", .6, .9, .75, -1, 1}, Profile{"Equilibrado", 1, 1, 1, -1, 1},
			 Profile{"Detalle", 1, 1.05, 1.35, .25, 1}, Profile{"Cinematográfico", 1, 1.2, 1.1, -1, 2}})
	{
		auto* button = new QPushButton(QString::fromUtf8(p.name), presets);
		preset_row->addWidget(button);
		connect(button, &QPushButton::clicked, this, [this, p]() {
			setValue("NeuralUplift", "1");
			setValue("EnableHooks", "2");
			setValue("NRIntensity", QString::number(p.intensity));
			setValue("NRLocalTone", QString::number(p.tone));
			setValue("NRLocalStructure", QString::number(p.structure));
			setValue("NRSkinStructure", QString::number(p.skin));
			setValue("NRStyle", QString::number(p.style));
			setValue("NRPaperWhiteScale", "15.401");
			setValue("NRTransferStrength", "1");
			setValue("NRColorStrength", "1");
			setValue("NRAutoMask", "1");
			setValue("NRUICorrection", "1");
			setValue("NREnableUpscaling", "0");
			setValue("NRPreset", "0");
			setValue("enabled", "1", true);
			setValue("mode", "2", true);
			m_configuration.preset = neural_rendering::default_preset();
			refresh();
			m_status->setText(tr("Preset preparado. Pulsa Aplicar para verlo en el juego."));
		});
	}
	neural->addRow(tr("Preparación rápida"), presets);
	m_neural = new QCheckBox(tr("Procesamiento neural (además de ReShade)"), this);
	neural->addRow(m_neural);
	connect(m_neural, &QCheckBox::toggled, this, [this](bool enabled) {
		if (!m_refreshing)
			setValue("NeuralUplift", enabled ? "1" : "0");
	});
	addSlider(neural, tr("Intensidad"), "NRIntensity", 0, 2, 1);
	addSlider(neural, tr("Tono local"), "NRLocalTone", 0, 2, 1);
	addSlider(neural, tr("Estructura local"), "NRLocalStructure", 0, 2, 1);
	addSlider(neural, tr("Estructura de piel"), "NRSkinStructure", -1, 1, -1);
	addChoice(neural, tr("Estilo"), "NRStyle", {{0, tr("Predeterminado")}, {1, tr("Natural")}, {2, tr("Cinematográfico")}}, 1);
	addSlider(neural, tr("Blanco de referencia"), "NRPaperWhiteScale", 0, 16, 15.401);
	addSlider(neural, tr("Transferencia"), "NRTransferStrength", 0, 1, 1);
	addSlider(neural, tr("Color"), "NRColorStrength", 0, 1, 1);
	addChoice(neural, tr("Máscara automática"), "NRAutoMask", {{0, tr("Desactivada")}, {1, tr("Activada")}}, 1);
	addChoice(neural, tr("Corrección de interfaz"), "NRUICorrection", {{0, tr("Desactivada")}, {1, tr("Activada")}}, 1);
	addChoice(neural, tr("Preset del modelo"), "NRPreset", {{0, tr("Predeterminado")}, {1, tr("Preset 1")}, {2, tr("Preset 2")}, {3, tr("Preset 3")}}, 0);
	addChoice(neural, tr("Hooks"), "EnableHooks", {{0, tr("Desactivados")}, {1, tr("Completos")}, {2, tr("NGX / Feeder")}}, 2);
	addChoice(neural, tr("Reescalado neural"), "NREnableUpscaling", {{0, tr("Desactivado")}, {1, tr("Activado")}}, 0);
	addChoice(neural, tr("Profundidad neural"), "NRDepthMode", {{0, tr("Flag NGX")}, {1, tr("Normal")}, {2, tr("Invertida")}}, 0);
	addSlider(neural, tr("Movimiento neural X"), "NRMVecScaleX", 0, 4, 1);
	addSlider(neural, tr("Movimiento neural Y"), "NRMVecScaleY", 0, 4, 1);
	auto* feeder = page(tr("Feeder"));
	addChoice(feeder, tr("Transporte"), "enabled", {{0, tr("Desactivado")}, {1, tr("Activado")}}, 1, true);
	addChoice(feeder, tr("Modo"), "mode", {{0, tr("Inactivo")}, {1, tr("Solo transporte (sin neural)")}, {2, tr("DLSS + neural")}}, 2, true);
	addChoice(feeder, tr("Espacio de color"), "hdr", {{-1, tr("Automático")}, {0, tr("SDR")}, {1, tr("HDR")}}, 1, true);
	addChoice(feeder, tr("Profundidad invertida"), "depth_inverted", {{-1, tr("Automática")}, {0, tr("Normal")}, {1, tr("Invertida")}}, 1, true);
	addChoice(feeder, tr("Preset DLSS"), "preset", {{0, tr("Automático")}, {5, "E"}, {6, "F"}, {10, "J"}, {11, "K"}}, 0, true);
	addChoice(feeder, tr("Sincronización Vulkan"), "vk_present_sync", {{0, tr("Desactivada")}, {1, tr("Activada")}}, 1, true);
	addSlider(feeder, tr("Retardo de creación (fotogramas)"), "create_delay", 0, 600, 60, true, 0);
	addSlider(feeder, tr("Calentamiento (fotogramas)"), "warmup_rebuild", 0, 1800, 180, true, 0);
	addSlider(feeder, tr("Tiempo límite de GPU (ms)"), "gpu_timeout_ms", 0, 10000, 2000, true, 0);
	addSlider(feeder, tr("Movimiento X"), "mv_scale_x", -4, 4, 1, true);
	addSlider(feeder, tr("Movimiento Y"), "mv_scale_y", -4, 4, 1, true);
	auto* reshade = page(tr("ReShade"));
	auto* performance = new QCheckBox(tr("Compilar efectos en modo rendimiento"), this);
	performance->setChecked(neural_rendering::ini_value(m_configuration.config, "GENERAL", "PerformanceMode", "1") == "1");
	reshade->addRow(performance);
	connect(performance, &QCheckBox::toggled, this, [this](bool enabled) {
		neural_rendering::set_ini_value(m_configuration.config, "GENERAL", "PerformanceMode", enabled ? "1" : "0");
	});
	for (const auto& field : {std::pair{"EffectSearchPaths", "Carpetas de efectos"}, std::pair{"TextureSearchPaths", "Carpetas de texturas"},
			 std::pair{"PreprocessorDefinitions", "Definiciones globales"}})
	{
		auto* edit = new QLineEdit(neural_rendering::ini_value(m_configuration.config, "GENERAL", field.first), this);
		reshade->addRow(QString::fromUtf8(field.second), edit);
		connect(edit, &QLineEdit::textChanged, this, [this, key = QString::fromUtf8(field.first)](const QString& text) {
			neural_rendering::set_ini_value(m_configuration.config, "GENERAL", key, text);
		});
	}
	auto* safety = new QLabel(tr("Los atajos de ReShade están deshabilitados permanentemente. Los presets preparan Lumenite → Feeder. Las opciones del modelo dependen del componente instalado; un ajuste disponible no garantiza un cambio visual en todos los juegos."), this);
	safety->setWordWrap(true);
	reshade->addRow(safety);
	m_log = new QPlainTextEdit(this);
	m_log->setReadOnly(true);
	auto* diagnostics = new QWidget(this);
	auto* diagnostics_layout = new QVBoxLayout(diagnostics);
	diagnostics_layout->addWidget(m_log);
	auto* refresh_log = new QPushButton(tr("Actualizar diagnóstico"), diagnostics);
	diagnostics_layout->addWidget(refresh_log);
	connect(refresh_log, &QPushButton::clicked, this, [this]() {
		m_log->setPlainText(neural_rendering::diagnostics() + "\n" + m_download_log);
	});
	tabs->addTab(diagnostics, tr("Diagnóstico"));
	auto* buttons = new QHBoxLayout();
	auto* install = new QPushButton(tr("Descargar / reparar componentes"), this);
	install->setObjectName("neural_download_components");
	buttons->addWidget(install);
	connect(install, &QPushButton::clicked, this, [this]() { download(); });
	auto* vulkan = new QPushButton(tr("Seleccionar Vulkan"), this);
	buttons->addWidget(vulkan);
	connect(vulkan, &QPushButton::clicked, this, std::move(select_vulkan));
	m_apply = new QPushButton(tr("Aplicar sin reiniciar"), this);
	m_apply->setObjectName("neural_apply");
	buttons->addWidget(m_apply);
	connect(m_apply, &QPushButton::clicked, this, [this]() { apply(); });
	layout->addLayout(buttons);
	m_status = new QLabel(this);
	m_status->setWordWrap(true);
	layout->addWidget(m_status);
	refresh();
}

QString NeuralRenderingSettingsWidget::value(const QString& key, const QString& fallback, bool feeder) const
{
	return neural_rendering::ini_value(feeder ? m_configuration.feeder : m_configuration.config,
		feeder ? QString() : QStringLiteral("RenoDX.DLSS5"), key, fallback);
}
void NeuralRenderingSettingsWidget::setValue(const QString& key, const QString& text, bool feeder)
{
	neural_rendering::set_ini_value(feeder ? m_configuration.feeder : m_configuration.config,
		feeder ? QString() : QStringLiteral("RenoDX.DLSS5"), key, text);
	if (m_status && !m_refreshing)
		m_status->setText(tr("Cambios pendientes. Pulsa Aplicar sin reiniciar."));
}
void NeuralRenderingSettingsWidget::addSlider(QFormLayout* form, const QString& label, const QString& key,
	double minimum, double maximum, double fallback, bool feeder, int decimals)
{
	auto* row = new QWidget(this);
	auto* layout = new QHBoxLayout(row);
	layout->setContentsMargins(0, 0, 0, 0);
	auto* slider = new QSlider(Qt::Horizontal, row);
	slider->setObjectName("neural_" + key);
	const double scale = std::pow(10., decimals);
	slider->setRange(static_cast<int>(std::round(minimum * scale)), static_cast<int>(std::round(maximum * scale)));
	auto* number = new QLabel(row);
	number->setMinimumWidth(60);
	number->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	layout->addWidget(slider, 1);
	layout->addWidget(number);
	form->addRow(label, row);
	m_refreshers.push_back([=, this]() {
		QSignalBlocker block(slider);
		bool ok = false;
		double v = value(key, QString::number(fallback), feeder).toDouble(&ok);
		if (!ok || !std::isfinite(v))
			v = fallback;
		v = std::clamp(v, minimum, maximum);
		slider->setValue(static_cast<int>(std::round(v * scale)));
		number->setText(QString::number(v, 'f', decimals));
	});
	connect(slider, &QSlider::valueChanged, this, [=, this](int v) {
		number->setText(QString::number(v / scale, 'f', decimals));
		if (!m_refreshing)
			setValue(key, number->text(), feeder);
	});
}
void NeuralRenderingSettingsWidget::addChoice(QFormLayout* form, const QString& label, const QString& key,
	std::initializer_list<std::pair<int, QString>> choices, int fallback, bool feeder)
{
	auto* combo = new QComboBox(this);
	combo->setObjectName("neural_" + key);
	for (const auto& [id, name] : choices)
		combo->addItem(name, id);
	form->addRow(label, combo);
	m_refreshers.push_back([=, this]() {
		QSignalBlocker block(combo);
		const int index = combo->findData(value(key, QString::number(fallback), feeder).toInt());
		combo->setCurrentIndex(index < 0 ? combo->findData(fallback) : index);
	});
	connect(combo, &QComboBox::currentIndexChanged, this, [=, this]() {
		if (!m_refreshing)
			setValue(key, QString::number(combo->currentData().toInt()), feeder);
	});
}
void NeuralRenderingSettingsWidget::refresh()
{
	m_refreshing = true;
	m_enabled->setChecked(m_configuration.enabled);
	m_neural->setChecked(value("NeuralUplift", "1") == "1");
	for (const auto& fn : m_refreshers)
		fn();
	m_log->setPlainText(neural_rendering::diagnostics() + "\n" + m_download_log);
	m_status->setText(m_configuration.enabled ? tr("Activación guardada. Consulta Diagnóstico para comprobar la evaluación neural.") : tr("Desactivado. Actívalo aquí cuando quieras; no necesitas reiniciar PCSX2."));
	m_refreshing = false;
}
void NeuralRenderingSettingsWidget::apply()
{
	if (NeuralRendering::IsApplying())
		return;
	setEnabled(false);
	m_status->setText(tr("Aplicando y conservando el estado de la partida…"));
	QPointer<NeuralRenderingSettingsWidget> self(this);
	NeuralRendering::Apply(m_configuration, [self](QString error) {
		if (!self)
			return;
		self->setEnabled(true);
		self->m_configuration = NeuralRendering::Load();
		self->refresh();
		self->m_status->setText(error.isEmpty() ? self->tr("Aplicado sin reiniciar PCSX2. Menú ReShade bloqueado.") : error);
	});
}
void NeuralRenderingSettingsWidget::download(bool activate_after)
{
	const QString root = neural_rendering::root_path();
	const QString script = QDir(root).filePath("Setup-Neural.ps1");
	if (!QFileInfo::exists(script))
	{
		m_configuration.enabled = neural_rendering::enabled();
		refresh();
		m_status->setText(tr("Falta Setup-Neural.ps1 en la carpeta del emulador."));
		return;
	}
	setEnabled(false);
	m_status->setText(tr("Descargando componentes desde sus autores…"));
	m_download_log.clear();
	auto* process = new QProcess(this);
	process->setProcessChannelMode(QProcess::MergedChannels);
	connect(process, &QProcess::readyReadStandardOutput, this, [this, process]() {
		const QString output = QString::fromUtf8(process->readAllStandardOutput());
		m_download_log += output;
		m_log->appendPlainText(output);
	});
	connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError e) {
		if (e != QProcess::FailedToStart)
			return;
		setEnabled(true);
		m_configuration.enabled = neural_rendering::enabled();
		refresh();
		m_status->setText(process->errorString());
		process->deleteLater();
	});
	connect(process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this, [this, process, activate_after](int code, QProcess::ExitStatus status) {
		setEnabled(true);
		process->deleteLater();
		const QString problem = neural_rendering::validate_runtime();
		if (code || status != QProcess::NormalExit || !problem.isEmpty())
		{
			m_configuration.enabled = neural_rendering::enabled();
			refresh();
			m_status->setText(tr("No se completó la instalación. Consulta Diagnóstico. ") + problem);
			return;
		}
		m_status->setText(tr("Componentes instalados."));
		if (activate_after)
		{
			m_configuration.enabled = true;
			apply();
		}
	});
	process->setWorkingDirectory(root);
	process->start("powershell.exe", {"-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-File", script,
										 "-Pcsx2Directory", root, "-FromPcsx2Id", QString::number(QCoreApplication::applicationPid())});
}
