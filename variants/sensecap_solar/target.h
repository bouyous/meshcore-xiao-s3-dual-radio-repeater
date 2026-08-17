#pragma once

#define RADIOLIB_STATIC_ONLY 1
#include <RadioLib.h>
#include <helpers/radiolib/RadioLibWrappers.h>
#include <SenseCapSolarBoard.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/ArduinoHelpers.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#if defined(P1_EVENT_LOG)
#include <P1EventJournal.h>
#endif

extern SenseCapSolarBoard board;
extern WRAPPER_CLASS radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
#if defined(P1_EVENT_LOG)
extern P1EventJournal p1_event_journal;
#endif

bool radio_init();
mesh::LocalIdentity radio_new_identity();

