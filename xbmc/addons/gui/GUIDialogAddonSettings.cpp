/*
 *  Copyright (C) 2005-2018 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "GUIDialogAddonSettings.h"

#include "FileItem.h"
#include "GUIPassword.h"
#include "GUIUserMessages.h"
#include "ServiceBroker.h"
#include "addons/AddonManager.h"
#include "addons/settings/AddonSettings.h"
#include "dialogs/GUIDialogSelect.h"
#include "dialogs/GUIDialogYesNo.h"
#include "guilib/GUIComponent.h"
#include "guilib/GUIWindowManager.h"
#include "guilib/LocalizeStrings.h"
#include "guilib/WindowIDs.h"
#include "input/Key.h"
#include "messaging/helpers/DialogOKHelper.h"
#include "settings/Settings.h"
#include "settings/SettingsComponent.h"
#include "settings/lib/SettingsManager.h"
#include "utils/StringUtils.h"
#include "utils/Variant.h"
#include "utils/log.h"
#include "view/ViewStateSettings.h"

#define CONTROL_BTN_LEVELS 20

using namespace ADDON;
using namespace KODI::MESSAGING;

CGUIDialogAddonSettings::CGUIDialogAddonSettings()
  : CGUIDialogSettingsManagerBase(WINDOW_DIALOG_ADDON_SETTINGS, "DialogAddonSettings.xml")
{
}

bool CGUIDialogAddonSettings::OnMessage(CGUIMessage& message)
{
  switch (message.GetMessage())
  {
    case GUI_MSG_CLICKED:
    {
      if (message.GetSenderId() == CONTROL_SETTINGS_CUSTOM_BUTTON)
      {
        OnResetSettings();
        return true;
      }
      break;
    }

    case GUI_MSG_SETTING_UPDATED:
    {
      const std::string& settingId = message.GetStringParam(0);
      const std::string& settingValue = message.GetStringParam(1);
      const ADDON::AddonInstanceId instanceId = message.GetParam1();

      if (instanceId != m_instanceId)
      {
        CLog::Log(LOGERROR,
                  "CGUIDialogAddonSettings::{}: Set value \"{}\" from add-on \"{}\" called with "
                  "invalid instance id (given: {}, needed: {})",
                  __func__, m_addon->ID(), settingId, instanceId, m_instanceId);
        break;
      }

      std::shared_ptr<CSetting> setting = GetSettingsManager()->GetSetting(settingId);
      if (setting != nullptr)
      {
        setting->FromString(settingValue);
        return true;
      }
      break;
    }

    default:
      break;
  }

  return CGUIDialogSettingsManagerBase::OnMessage(message);
}

bool CGUIDialogAddonSettings::OnAction(const CAction& action)
{
  switch (action.GetID())
  {
    case ACTION_SETTINGS_LEVEL_CHANGE:
    {
      // Test if we can access the new level
      if (!g_passwordManager.CheckSettingLevelLock(
              CViewStateSettings::GetInstance().GetNextSettingLevel(), true))
        return false;

      CViewStateSettings::GetInstance().CycleSettingLevel();
      CServiceBroker::GetSettingsComponent()->GetSettings()->Save();

      // try to keep the current position
      std::string oldCategory;
      if (m_iCategory >= 0 && m_iCategory < static_cast<int>(m_categories.size()))
        oldCategory = m_categories[m_iCategory]->GetId();

      SET_CONTROL_LABEL(CONTROL_BTN_LEVELS,
                        10036 +
                            static_cast<int>(CViewStateSettings::GetInstance().GetSettingLevel()));
      // only re-create the categories, the settings will be created later
      SetupControls(false);

      m_iCategory = 0;
      // try to find the category that was previously selected
      if (!oldCategory.empty())
      {
        for (int i = 0; i < static_cast<int>(m_categories.size()); i++)
        {
          if (m_categories[i]->GetId() == oldCategory)
          {
            m_iCategory = i;
            break;
          }
        }
      }

      CreateSettings();
      return true;
    }

    default:
      break;
  }

  return CGUIDialogSettingsManagerBase::OnAction(action);
}

bool CGUIDialogAddonSettings::ShowForAddon(const ADDON::AddonPtr& addon,
                                           bool saveToDisk /* = true */)
{
  if (addon == nullptr)
    return false;

  if (!g_passwordManager.CheckMenuLock(WINDOW_ADDON_BROWSER))
    return false;

  bool byNew = false;
  bool enabled = false;
  ADDON::AddonInstanceId instanceId = ADDON::ADDON_SETTINGS_ID;

  if (addon->SupportsInstanceSettings())
  {
    static constexpr int ADDON_SETTINGS = 0;
    static constexpr int ADD_INSTANCE = 100;
    static constexpr int REMOVE_INSTANCE = 101;
    static constexpr int GENERAL_BUTTON_START = ADD_INSTANCE;

    CGUIDialogSelect* dialog =
        CServiceBroker::GetGUI()->GetWindowManager().GetWindow<CGUIDialogSelect>(
            WINDOW_DIALOG_SELECT);
    if (dialog)
    {
      while (true)
      {
        const std::vector<ADDON::AddonInstanceId> ids = addon->GetKnownInstanceIds();

        CFileItemList itemsGeneral;
        CFileItemList itemsInstances;

        dialog->Reset();
        dialog->SetHeading(10012); // Add-on configurations and settings
        dialog->SetUseDetails(false);

        {
          const CFileItemPtr item =
              std::make_shared<CFileItem>(g_localizeStrings.Get(10014)); // Add add-on configuration
          item->SetProperty("id", ADD_INSTANCE);
          itemsGeneral.Add(item);
        }
        // Have always minimal 1 available and not allow this button in this case
        if (ids.size() > 1)
        {
          const CFileItemPtr item = std::make_shared<CFileItem>(
              g_localizeStrings.Get(10015)); // Remove add-on configuration
          item->SetProperty("id", REMOVE_INSTANCE);
          itemsGeneral.Add(item);
        }
        if (addon->HasSettings(ADDON_SETTINGS_ID))
        {
          const CFileItemPtr item =
              std::make_shared<CFileItem>(g_localizeStrings.Get(10013)); // Edit Add-on settings
          item->SetProperty("id", ADDON_SETTINGS);
          itemsGeneral.Add(item);
        }

        ADDON::AddonInstanceId highestId = 0;
        for (const auto& id : ids)
        {
          std::string name;
          addon->GetSettingString(ADDON_SETTING_INSTANCE_NAME_VALUE, name, id);
          if (name.empty())
            name = g_localizeStrings.Get(13205); // Unknown

          bool enabled{false};
          addon->GetSettingBool(ADDON_SETTING_INSTANCE_ENABLED_VALUE, enabled, id);

          const std::string label = StringUtils::Format(
              g_localizeStrings.Get(10020), name,
              g_localizeStrings.Get(enabled ? 305 : 13106)); // Edit "config name" [enabled state]

          const CFileItemPtr item = std::make_shared<CFileItem>(label);
          item->SetProperty("id", id);
          item->SetProperty("name", name);
          itemsInstances.Add(item);
          if (id > highestId)
            highestId = id;
        }

        for (auto& it : itemsGeneral)
          dialog->Add(*it);

        for (auto& it : itemsInstances)
          dialog->Add(*it);

        dialog->SetButtonFocus(true);
        dialog->Open();

        if (dialog->IsButtonPressed())
          return false;

        if (dialog->IsConfirmed())
        {
          const CFileItemPtr& item = dialog->GetSelectedFileItem();
          instanceId = item->GetProperty("id").asInteger();
          if (instanceId < GENERAL_BUTTON_START)
          {
            break;
          }
          else if (instanceId == ADD_INSTANCE)
          {
            instanceId = highestId + 1;
            addon->GetSettings(instanceId);
            addon->UpdateSettingString(ADDON_SETTING_INSTANCE_NAME_VALUE, "", instanceId);
            addon->UpdateSettingBool(ADDON_SETTING_INSTANCE_ENABLED_VALUE, true, instanceId);
            addon->SaveSettings(instanceId);
            byNew = true;
            break;
          }
          else if (instanceId == REMOVE_INSTANCE)
          {
            dialog->Reset();
            dialog->SetHeading(10010); // Select add-on configuration to remove
            dialog->SetUseDetails(false);
            for (auto& it : itemsInstances)
            {
              CFileItem item(*it);
              item.SetLabel((*it).GetProperty("name").asString());
              dialog->Add(item);
            }
            dialog->SetButtonFocus(true);
            dialog->Open();
            if (dialog->IsButtonPressed())
              continue;

            if (dialog->IsConfirmed())
            {
              const CFileItemPtr& item = dialog->GetSelectedFileItem();
              const std::string label = StringUtils::Format(
                  g_localizeStrings.Get(10019),
                  item->GetProperty("name")
                      .asString()); // Do you want to remove the add-on configuration "config name"?

              if (CGUIDialogYesNo::ShowAndGetInput(10009, // Confirm add-on configuration removal
                                                   label))
              {
                instanceId = item->GetProperty("id").asInteger();
                addon->DeleteInstanceSettings(instanceId);
                CServiceBroker::GetAddonMgr().PublishInstanceRemoved(addon->ID(), instanceId);
              }

              return false;
            }
          }
        }
        else
          return false;
      }
    }

    addon->GetSettingBool(ADDON_SETTING_INSTANCE_ENABLED_VALUE, enabled, instanceId);
  }

  if (!addon->HasSettings(instanceId))
  {
    // addon does not support settings, inform user
    HELPERS::ShowOKDialogText(CVariant{24000}, CVariant{24030});
    return false;
  }

  // Create the dialog
  CGUIDialogAddonSettings* dialog =
      CServiceBroker::GetGUI()->GetWindowManager().GetWindow<CGUIDialogAddonSettings>(
          WINDOW_DIALOG_ADDON_SETTINGS);
  if (dialog == nullptr)
    return false;

  dialog->m_addon = addon;
  dialog->m_instanceId = instanceId;
  dialog->m_saveToDisk = saveToDisk;
  dialog->Open();

  if (!dialog->IsConfirmed())
  {
    // Remove instance settings if added by new and not set
    if (byNew)
      addon->DeleteInstanceSettings(instanceId);
    return false;
  }

  if (saveToDisk)
    addon->SaveSettings(dialog->m_instanceId);

  // If added new, publish his add to related places and start the use of new instance
  if (instanceId != ADDON_SETTINGS_ID)
  {
    bool enabledNow = false;
    addon->GetSettingBool(ADDON_SETTING_INSTANCE_ENABLED_VALUE, enabledNow, instanceId);
    if (byNew || enabled != enabledNow)
    {
      CServiceBroker::GetAddonMgr().PublishInstanceAdded(addon->ID(), instanceId);
    }
    else if (!enabledNow && enabled != enabledNow)
    {
      CServiceBroker::GetAddonMgr().PublishInstanceRemoved(addon->ID(), instanceId);
    }
  }

  return true;
}

void CGUIDialogAddonSettings::SaveAndClose()
{
  if (!g_passwordManager.CheckMenuLock(WINDOW_ADDON_BROWSER))
    return;

  // get the dialog
  CGUIDialogAddonSettings* dialog =
      CServiceBroker::GetGUI()->GetWindowManager().GetWindow<CGUIDialogAddonSettings>(
          WINDOW_DIALOG_ADDON_SETTINGS);
  if (dialog == nullptr || !dialog->IsActive())
    return;

  // check if we need to save the settings
  if (dialog->m_saveToDisk && dialog->m_addon != nullptr)
    dialog->m_addon->SaveSettings(dialog->m_instanceId);

  // close the dialog
  dialog->Close();
}

std::string CGUIDialogAddonSettings::GetCurrentAddonID() const
{
  if (m_addon == nullptr)
    return "";

  return m_addon->ID();
}

void CGUIDialogAddonSettings::SetupView()
{
  if (m_addon == nullptr || m_addon->GetSettings(m_instanceId) == nullptr)
    return;

  auto settings = m_addon->GetSettings(m_instanceId);
  if (!settings->IsLoaded())
    return;

  CGUIDialogSettingsManagerBase::SetupView();

  // set addon id as window property
  SetProperty("Addon.ID", m_addon->ID());

  // set heading
  SetHeading(StringUtils::Format("$LOCALIZE[10004] - {}",
                                 m_addon->Name())); // "Settings - AddonName"

  // set control labels
  SET_CONTROL_LABEL(CONTROL_SETTINGS_OKAY_BUTTON, 186);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CANCEL_BUTTON, 222);
  SET_CONTROL_LABEL(CONTROL_SETTINGS_CUSTOM_BUTTON, 409);
  SET_CONTROL_LABEL(CONTROL_BTN_LEVELS,
                    10036 + static_cast<int>(CViewStateSettings::GetInstance().GetSettingLevel()));
}

std::string CGUIDialogAddonSettings::GetLocalizedString(uint32_t labelId) const
{
  std::string label = g_localizeStrings.GetAddonString(m_addon->ID(), labelId);
  if (!label.empty())
    return label;

  return CGUIDialogSettingsManagerBase::GetLocalizedString(labelId);
}

std::string CGUIDialogAddonSettings::GetSettingsLabel(const std::shared_ptr<ISetting>& setting)
{
  if (setting == nullptr)
    return "";

  std::string label = GetLocalizedString(setting->GetLabel());
  if (!label.empty())
    return label;

  // try the addon settings
  label = m_addon->GetSettings(m_instanceId)->GetSettingLabel(setting->GetLabel());
  if (!label.empty())
    return label;

  return CGUIDialogSettingsManagerBase::GetSettingsLabel(setting);
}

int CGUIDialogAddonSettings::GetSettingLevel() const
{
  return static_cast<int>(CViewStateSettings::GetInstance().GetSettingLevel());
}

std::shared_ptr<CSettingSection> CGUIDialogAddonSettings::GetSection()
{
  const auto settingsManager = GetSettingsManager();
  if (settingsManager == nullptr)
    return nullptr;

  const auto sections = settingsManager->GetSections();
  if (!sections.empty())
    return sections.front();

  return nullptr;
}

CSettingsManager* CGUIDialogAddonSettings::GetSettingsManager() const
{
  if (m_addon == nullptr || m_addon->GetSettings(m_instanceId) == nullptr)
    return nullptr;

  return m_addon->GetSettings(m_instanceId)->GetSettingsManager();
}

void CGUIDialogAddonSettings::OnSettingAction(const std::shared_ptr<const CSetting>& setting)
{
  if (m_addon == nullptr || m_addon->GetSettings(m_instanceId) == nullptr)
    return;

  m_addon->GetSettings(m_instanceId)->OnSettingAction(setting);
}
