#pragma once

#include <QString>

// Portable, process-local ReShade/Feeder integration. No driver or registry changes.
namespace neural_rendering
{
	QString root_path();
	QString config_path();
	QString preset_path();
	QString read_text(const QString& path, QString* error = nullptr);
	bool write_text(const QString& path, const QString& text, QString* error = nullptr);
	QString ini_value(const QString& text, const QString& section, const QString& key, const QString& fallback = {});
	void set_ini_value(QString& text, const QString& section, const QString& key, const QString& value);
	QString default_config();
	QString default_preset();
	// Enforce the portable integration's settings-only menu policy.
	QString settings_only_config(QString text);
	bool enabled();
	bool save_enabled(bool value, QString* error = nullptr);
	// The optional directory also allows validating an extracted package before use.
	QString validate_runtime(const QString& directory = root_path());
	QString diagnostics();
	// Call before Vulkan instance creation and again only after the old GS device has closed.
	QString initialize();
	bool prepared();
} // namespace neural_rendering
