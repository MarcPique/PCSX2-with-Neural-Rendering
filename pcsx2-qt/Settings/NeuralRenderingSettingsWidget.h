// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "Neural/NeuralRenderingController.h"
#include <QWidget>
#include <vector>
#include <functional>
class QCheckBox;
class QLabel;
class QFormLayout;
class QPlainTextEdit;
class QPushButton;

class NeuralRenderingSettingsWidget final : public QWidget
{
public:
	explicit NeuralRenderingSettingsWidget(std::function<void()> select_vulkan, QWidget* parent = nullptr);

private:
	void addSlider(QFormLayout* form, const QString& label, const QString& key,
		double minimum, double maximum, double fallback, bool feeder = false, int decimals = 2);
	void addChoice(QFormLayout* form, const QString& label, const QString& key,
		std::initializer_list<std::pair<int, QString>> choices, int fallback, bool feeder = false);
	void refresh();
	void apply();
	void download(bool activate_after = false);
	void setValue(const QString& key, const QString& value, bool feeder = false);
	QString value(const QString& key, const QString& fallback, bool feeder = false) const;
	NeuralRendering::Configuration m_configuration;
	std::vector<std::function<void()>> m_refreshers;
	QCheckBox* m_enabled = nullptr;
	QCheckBox* m_neural = nullptr;
	QLabel* m_status = nullptr;
	QPlainTextEdit* m_log = nullptr;
	QPushButton* m_apply = nullptr;
	bool m_refreshing = false;
	QString m_download_log;
};
