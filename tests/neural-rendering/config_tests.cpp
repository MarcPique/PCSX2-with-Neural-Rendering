#include "neural_rendering_config.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

int main(int argc, char** argv)
{
	QCoreApplication app(argc, argv);
	int failures = 0;
	const auto check = [&failures](bool condition, const char* label) {
		QTextStream(condition ? stdout : stderr) << (condition ? "PASS " : "FAIL ") << label << '\n';
		if (!condition)
			++failures;
	};
	using namespace neural_rendering;
	QString ini = QString::fromUtf8("\xef\xbb\xbf;keep comment\r\nTechniques=A@a.fx,B@b.fx\r\n[GENERAL]\r\nPerformanceMode=0\r\nEffectSearchPaths=.\\one,.\\two\r\n[Unknown.Addon]\r\n;keep me too\r\nMixed=1,2,,3;literal\r\n");
	check(ini_value(ini, "", "Techniques") == "A@a.fx,B@b.fx", "read root key with BOM/CRLF");
	check(ini_value(ini, "Unknown.Addon", "Mixed") == "1,2,,3;literal", "preserve vector and literal semicolon");
	check(ini_value(ini, "missing", "absent", "fallback") == "fallback", "missing value fallback");
	check(ini_value("[GENERAL] ; note\nKey=value\n", "GENERAL", "Key") == "value", "ReShade section with trailing comment");
	check(ini_value("[A]\nKey=one\nKey=two\n", "A", "Key") == "one,two", "ReShade duplicate values append");
	set_ini_value(ini, "GENERAL", "PerformanceMode", "1");
	check(ini_value(ini, "GENERAL", "PerformanceMode") == "1", "update existing setting");
	check(ini.contains(";keep comment") && ini.contains(";keep me too") && ini.contains("Mixed=1,2,,3;literal"), "preserve unknown keys and comments");
	set_ini_value(ini, "GENERAL", "PresetPath", ".\\ñandú\\preset.ini");
	check(ini_value(ini, "GENERAL", "PresetPath") == ".\\ñandú\\preset.ini", "insert into existing section and retain Unicode");
	set_ini_value(ini, "NEW", "Key", "value");
	check(ini_value(ini, "NEW", "Key") == "value", "append new section");
	set_ini_value(ini, "", "TechniqueSorting", "A,B");
	check(ini_value(ini, "", "TechniqueSorting") == "A,B", "insert root setting before first section");
	QString duplicates = "[A]\nK=one\n;comment\nK=two\n[B]\nK=three\n[A]\nK=four\n";
	set_ini_value(duplicates, "A", "K", "new");
	check(duplicates.count("K=new") == 1 && ini_value(duplicates, "A", "K") == "new" && ini_value(duplicates, "B", "K") == "three", "replace duplicate keys without changing other sections");
	QString blank;
	set_ini_value(blank, "", "Techniques", "A");
	set_ini_value(blank, "GENERAL", "PerformanceMode", "0");
	check(ini_value(blank, "", "Techniques") == "A" && ini_value(blank, "GENERAL", "PerformanceMode") == "0", "construct empty config");
	check(ini_value(default_config(), "ADDON", "AddonPath") == ".\\", "default addon path");
	const QString guarded = settings_only_config("[INPUT]\nKeyOverlay=36,0,0,0\n[ADDON]\nAddonPath=elsewhere\nDisabledAddons=Other,,Name,PCSX2 Settings Only,Other@pcsx2-settings-only.addon64,Keep@other.addon64\n[RenoDX.DLSS5]\nNRPaperWhiteScale=15.401\n");
	check(ini_value(guarded, "INPUT", "KeyOverlay") == "0,0,0,0", "overlay shortcut is removed");
	check(ini_value(guarded, "ADDON", "AddonPath") == ".\\", "guard loads from portable directory");
	check(ini_value(guarded, "ADDON", "DisabledAddons") == "Other,,Name,Keep@other.addon64", "guard cannot be disabled; unrelated escaped entries survive");
	check(ini_value(guarded, "RenoDX.DLSS5", "NRPaperWhiteScale") == "15.401", "menu policy preserves neural settings");
	check(settings_only_config(guarded) == guarded, "menu policy is idempotent");
	check(ini_value(default_config(), "RenoDX.DLSS5", "NeuralUplift") == "1" &&
			  ini_value(default_config(), "RenoDX.DLSS5", "NREnableUpscaling") == "0" &&
			  ini_value(default_config(), "RenoDX.DLSS5", "EnableHooks") == "2",
		"default neural consumer uses enabled NGX-only DLAA contract");
	check(ini_value(default_preset(), "", "Techniques").startsWith("Lumenite_Kernel@lumenite_Kernel.fx,DLSS5_Feed@"), "motion vectors precede neural feeder");
	QTemporaryDir temporary;
	check(temporary.isValid(), "temporary directory");
	const QString path = temporary.filePath("config.ini");
	QString error;
	check(write_text(path, ini, &error) && error.isEmpty(), "atomic creation");
	check(read_text(path, &error) == ini && error.isEmpty(), "UTF-8 round trip");
	check(write_text(path, ini + ";next\n", &error), "atomic update");
	check(read_text(path + ".pcsx2-backup", &error) == ini, "backup preserves previous file");
	error = "old";
	check(!read_text(temporary.filePath("missing.ini"), &error).size() && !error.isEmpty(), "missing file reports an error");
	check(!write_text(temporary.filePath("missing/directory/file.ini"), ini, &error) && !error.isEmpty(), "write failure reports an error");
#ifdef _WIN32
	const QDir fixture(temporary.filePath("paquete-ñandú"));
	for (const QString& name : {
			 QStringLiteral("ReShade64.dll"), QStringLiteral("neural-rendering/VkLayer_feed_vk.dll"),
			 QStringLiteral("dlss5-feed.addon64"), QStringLiteral("renodx-dlss5.addon64"),
			 QStringLiteral("pcsx2-settings-only.addon64"),
			 QStringLiteral("nvngx_dlssnr.dll"), QStringLiteral("nvngx_dlss.dll"),
			 QStringLiteral("reshade-shaders/Shaders/ReShade.fxh"),
			 QStringLiteral("reshade-shaders/Shaders/DLSS5_Feed.fx"),
			 QStringLiteral("reshade-shaders/Shaders/lumenite_Kernel.fx")})
	{
		const QString fixture_path = fixture.filePath(name);
		QDir().mkpath(QFileInfo(fixture_path).absolutePath());
		check(write_text(fixture_path, "fixture only, never loaded", &error), "create nonempty package fixture");
	}
	const auto manifest_text = [](const QString& name, const QString& library, const QString& disable_flag) {
		return QString::fromUtf8(QJsonDocument(QJsonObject{{"file_format_version", "1.2.0"},
												   {"layer", QJsonObject{{"name", name}, {"library_path", library},
																 {"disable_environment", QJsonObject{{disable_flag, "1"}}}}}})
				.toJson());
	};
	const QString reshade_manifest = fixture.filePath("neural-rendering/ReShade64.json");
	const QString feeder_manifest = fixture.filePath("neural-rendering/VkLayer_feed_vk.json");
	const QString valid_reshade = manifest_text("VK_LAYER_PCSX2_reshade", "..\\ReShade64.dll", "DISABLE_VK_LAYER_PCSX2_reshade");
	const QString valid_feeder = manifest_text("VK_LAYER_feed_vk", ".\\VkLayer_feed_vk.dll", "DISABLE_VK_LAYER_feed_vk");
	check(write_text(reshade_manifest, valid_reshade, &error) && write_text(feeder_manifest, valid_feeder, &error), "create both layer manifests");
	check(validate_runtime(fixture.path()).isEmpty(), "complete portable package validates under Unicode path");
	write_text(feeder_manifest, manifest_text("VK_LAYER_wrong", ".\\VkLayer_feed_vk.dll", "DISABLE_VK_LAYER_feed_vk"), &error);
	check(!validate_runtime(fixture.path()).isEmpty(), "wrong Feeder layer name is rejected");
	write_text(feeder_manifest, manifest_text("VK_LAYER_feed_vk", "..\\ReShade64.dll", "DISABLE_VK_LAYER_feed_vk"), &error);
	check(!validate_runtime(fixture.path()).isEmpty(), "Feeder manifest cannot point at a different package DLL");
	write_text(feeder_manifest, manifest_text("VK_LAYER_feed_vk", ".\\VkLayer_feed_vk.dll", "WRONG_DISABLE_FLAG"), &error);
	check(!validate_runtime(fixture.path()).isEmpty(), "Feeder manifest must honor process disable flag");
	write_text(feeder_manifest, "{ invalid json", &error);
	check(!validate_runtime(fixture.path()).isEmpty(), "malformed layer manifest is rejected");
	write_text(feeder_manifest, valid_feeder, &error);
	write_text(reshade_manifest, manifest_text("VK_LAYER_PCSX2_reshade", ".\\VkLayer_feed_vk.dll", "DISABLE_VK_LAYER_PCSX2_reshade"), &error);
	check(!validate_runtime(fixture.path()).isEmpty(), "ReShade manifest cannot point at the Feeder DLL");
	write_text(reshade_manifest, manifest_text("VK_LAYER_PCSX2_reshade", "..\\ReShade64.dll", "WRONG_DISABLE_FLAG"), &error);
	check(!validate_runtime(fixture.path()).isEmpty(), "ReShade manifest must honor process disable flag");
	write_text(reshade_manifest, valid_reshade, &error);
	QFile::remove(fixture.filePath("neural-rendering/VkLayer_feed_vk.dll"));
	check(validate_runtime(fixture.path()).contains("VkLayer_feed_vk.dll"), "missing Feeder layer DLL is rejected");
#endif
	check(!validate_runtime().isEmpty(), "incomplete runtime is rejected");
	check(!save_enabled(true, &error) && !error.isEmpty(), "cannot enable incomplete runtime");
	qputenv("VK_INSTANCE_LAYERS", "VK_LAYER_test;VK_LAYER_reshade;VK_LAYER_feed_vk;VK_LAYER_PCSX2_reshade");
	check(initialize().contains("desactivada"), "integration defaults off");
	check(qgetenv("VK_INSTANCE_LAYERS") == "VK_LAYER_test", "disabled integration preserves unrelated Vulkan layers");
	check(qgetenv("DISABLE_VK_LAYER_reshade_1") == "1", "disabled integration blocks global implicit ReShade for this process");
	check(qgetenv("DISABLE_VK_LAYER_feed_vk") == "1", "disabled integration blocks implicit Feeder for this process");
	check(prepared(), "disabled environment preparation succeeds");
	check(qgetenv("RESHADE_DISABLE_GRAPHICS_HOOK") == "1", "Feeder private D3D12 device must not keep ReShade alive");
	check(qgetenv("RESHADE_DISABLE_INPUT_HOOK") == "1", "settings-only integration disables input hooks");
	check(initialize().contains("desactivada") && prepared(), "environment can be reconfigured in the same process");
	QTextStream(stdout) << "Failures: " << failures << '\n';
	return failures ? 1 : 0;
}
