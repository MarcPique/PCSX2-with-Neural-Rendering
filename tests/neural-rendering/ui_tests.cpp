// SPDX-License-Identifier: GPL-3.0-or-later
#include "Settings/NeuralRenderingSettingsWidget.h"
#include "Neural/neural_rendering_config.h"
#include <QApplication>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QPushButton>
#include <QComboBox>
#include <QTabWidget>
#include <QTextStream>
#include <QTest>
#include <QFontDatabase>
#include <cmath>

static NeuralRendering::Configuration s_saved;
static int s_applied = 0;
namespace NeuralRendering
{
	Configuration Load() { return s_saved; }
	bool IsApplying() { return false; }
	void Apply(Configuration config, std::function<void(QString)> done)
	{
		s_saved = std::move(config);
		++s_applied;
		done({});
	}
} // namespace NeuralRendering
int main(int argc, char** argv)
{
	QApplication app(argc, argv);
	const int font = QFontDatabase::addApplicationFont("C:/Windows/Fonts/segoeui.ttf");
	if (font >= 0)
		app.setFont(QFont(QFontDatabase::applicationFontFamilies(font).front(), 10));
	s_saved = {neural_rendering::default_config(), neural_rendering::default_preset(), "enabled=1\nmode=1\n", false};
	NeuralRenderingSettingsWidget widget([]() {});
	widget.resize(1060, 800);
	widget.show();
	QApplication::processEvents();
	int failures = 0;
	const auto check = [&](bool ok, const char* label) { QTextStream(stdout) << (ok?"PASS ":"FAIL ") << label << '\n'; failures += !ok; };
	check(widget.findChildren<QSlider*>().size() == 14, "all 14 numeric options have sliders");
	check(widget.findChildren<QSpinBox*>().isEmpty() && widget.findChildren<QDoubleSpinBox*>().isEmpty(), "no editable numeric fields");
	auto* intensity = widget.findChild<QSlider*>("neural_NRIntensity");
	intensity->setValue(137);
	widget.findChild<QPushButton*>("neural_apply")->click();
	check(neural_rendering::ini_value(s_saved.config, "RenoDX.DLSS5", "NRIntensity") == "1.37", "slider persists actual numeric value");
	check(neural_rendering::ini_value(s_saved.config, "RenoDX.DLSS5", "NRPaperWhiteScale") == "15.401", "untouched white value retains precision");
	for (const auto* name : {"Suave", "Equilibrado", "Detalle", "Cinematográfico"})
	{
		QPushButton* preset = nullptr;
		for (auto* button : widget.findChildren<QPushButton*>())
			if (button->text() == QString::fromUtf8(name))
				preset = button;
		check(preset != nullptr, name);
		if (preset)
			preset->click();
		widget.findChild<QPushButton*>("neural_apply")->click();
	}
	check(neural_rendering::ini_value(s_saved.config, "RenoDX.DLSS5", "NRStyle") == "2", "cinematic preset selects style 2");
	check(s_applied == 5, "native apply action routes snapshots to controller");
	check(neural_rendering::ini_value(s_saved.feeder, "", "mode") == "2", "presets enable evaluation, not transport-only mode");
	check(widget.grab().save("neural-settings.png"), "render native controls offscreen");
	widget.findChild<QTabWidget*>()->setCurrentIndex(1);
	QApplication::processEvents();
	check(widget.grab().save("neural-feeder.png"), "render Feeder controls offscreen");
	return failures ? 1 : 0;
}
