// SPDX-License-Identifier: GPL-3.0+
#pragma once

#include "common/MemorySettingsInterface.h"

class GSBackThreadTrialSettings
{
public:
	GSBackThreadTrialSettings(MemorySettingsInterface* settings, bool requested)
		: m_settings(requested ? settings : nullptr)
	{
		if (!m_settings)
			return;

		m_previous_present = m_settings->GetStringValue(Section, Key, &m_previous);
		m_settings->SetIntValue(Section, Key, 1);
	}

	~GSBackThreadTrialSettings()
	{
		if (!m_settings)
			return;

		if (m_previous_present)
			m_settings->SetStringValue(Section, Key, m_previous.c_str());
		else
			m_settings->DeleteValue(Section, Key);
	}

	GSBackThreadTrialSettings(const GSBackThreadTrialSettings&) = delete;
	GSBackThreadTrialSettings& operator=(const GSBackThreadTrialSettings&) = delete;

private:
	static constexpr const char* Section = "EmuCore/GS";
	static constexpr const char* Key = "GSBackThreadMode";
	MemorySettingsInterface* m_settings;
	std::string m_previous;
	bool m_previous_present = false;
};
