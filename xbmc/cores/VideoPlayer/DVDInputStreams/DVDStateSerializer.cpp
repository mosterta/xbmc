/*
 *  Copyright (C) 2005-2022 Team Kodi
 *  This file is part of Kodi - https://kodi.tv
 *
 *  SPDX-License-Identifier: GPL-2.0-or-later
 *  See LICENSES/README.md for more information.
 */

#include "DVDStateSerializer.h"

#include "utils/StringUtils.h"
#include "utils/XBMCTinyXML2.h"
#include "utils/log.h"

#include <cstring>
#include <sstream>

namespace
{
// Serializer version - used to avoid processing deprecated/legacy schemas
constexpr int DVDSTATESERIALIZER_VERSION = 2;
}

bool CDVDStateSerializer::DVDStateToXML(std::string& xmlstate, const DVDState& state)
{
  CXBMCTinyXML2 xmlDoc;

  auto* eRoot = xmlDoc.NewElement("navstate");
  if (eRoot == nullptr)
    return false;

  eRoot->SetAttribute("version", DVDSTATESERIALIZER_VERSION);

  AddXMLElement(*eRoot, "title", std::to_string(state.title).c_str());
  AddXMLElement(*eRoot, "pgn", std::to_string(state.pgn).c_str());
  AddXMLElement(*eRoot, "pgcn", std::to_string(state.pgcn).c_str());
  AddXMLElement(*eRoot, "current_angle", std::to_string(state.current_angle).c_str());
  AddXMLElement(*eRoot, "audio_num", std::to_string(state.audio_num).c_str());
  AddXMLElement(*eRoot, "subp_num", std::to_string(state.subp_num).c_str());
  AddXMLElement(*eRoot, "sub_enabled", state.sub_enabled ? "true" : "false");
  xmlDoc.InsertEndChild(eRoot);

  tinyxml2::XMLPrinter printer;
  xmlDoc.Accept(&printer);
  xmlstate = printer.CStr();
  return true;
}

bool CDVDStateSerializer::XMLToDVDState(DVDState& state, const std::string& xmlstate)
{
  CXBMCTinyXML2 xmlDoc;

  xmlDoc.Parse(xmlstate);
  if (xmlDoc.Error())
    return false;

  tinyxml2::XMLHandle hRoot(xmlDoc.RootElement());
  if (!hRoot.ToElement() || !StringUtils::EqualsNoCase(hRoot.ToElement()->Value(), "navstate"))
  {
    CLog::LogF(LOGERROR, "Failed to deserialize dvd state - failed to detect root element.");
    return false;
  }

  auto version = hRoot.ToElement()->Attribute("version");
  if (!version || !StringUtils::EqualsNoCase(version, std::to_string(DVDSTATESERIALIZER_VERSION)))
  {
    CLog::LogF(LOGERROR, "Failed to deserialize dvd state - incompatible serializer version.");
    return false;
  }

  const auto* childElement = hRoot.ToElement()->FirstChildElement();
  while (childElement != nullptr)
  {
    char *endPtr;
    const std::string property = childElement->Value();
    if (property == "title")
    {
      state.title = strtol(childElement->GetText(), &endPtr, 0);
    }
    else if (property == "pgn")
    {
      state.pgn = strtol(childElement->GetText(), &endPtr, 0);
    }
    else if (property == "pgcn")
    {
      state.pgcn = strtol(childElement->GetText(), &endPtr, 0);
    }
    else if (property == "current_angle")
    {
      state.current_angle = strtol(childElement->GetText(), &endPtr, 0);
    }
    else if (property == "subp_num")
    {
      state.subp_num = strtol(childElement->GetText(), &endPtr, 0);
    }
    else if (property == "audio_num")
    {
      state.audio_num = strtol(childElement->GetText(), &endPtr, 0);
    }
    else if (property == "sub_enabled")
    {
      state.sub_enabled = StringUtils::EqualsNoCase(childElement->GetText(), "true");
    }
    else
    {
      CLog::LogF(LOGWARNING, "Unmapped dvd state property {}, ignored.", childElement->Value());
    }
    childElement = childElement->NextSiblingElement();
  }
  return true;
}

void CDVDStateSerializer::AddXMLElement(tinyxml2::XMLElement& root,
                                        const std::string& name,
                                        const std::string& value)
{
  auto* xmlDoc = root.GetDocument();
  auto* xmlElement = xmlDoc->NewElement(name.c_str());
  auto* xmlElementValue = xmlDoc->NewText(value.c_str());
  xmlElement->InsertEndChild(xmlElementValue);
  root.InsertEndChild(xmlElement);
}
