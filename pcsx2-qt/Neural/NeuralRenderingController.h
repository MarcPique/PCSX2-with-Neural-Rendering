// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <functional>

namespace NeuralRendering
{
	struct Configuration
	{
		QString config;
		QString preset;
		QString feeder;
		bool enabled = false;
	};

	Configuration Load();
	// Called on the UI thread. Completion also runs on the UI thread. File changes
	// occur after the old GS device has closed, so ReShade cannot overwrite them.
	void Apply(Configuration configuration, std::function<void(QString)> completion);
	bool IsApplying();
} // namespace NeuralRendering
