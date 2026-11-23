// SPDX-License-Identifier: GPL-3.0+
// No network transport is exposed by the interpreter-first Switch 2 port.
#include "net.h"
#include "common/Console.h"

NetAdapter* nif = nullptr;
std::thread rx_thread;
volatile bool RxRunning = false;

void InitNet()
{
	Console.Error("DEV9 Ethernet is unavailable in this Switch 2 build.");
	EmuConfig.DEV9.EthEnable = false;
}
void ReconfigureLiveNet(const Pcsx2Config&)
{
	if (EmuConfig.DEV9.EthEnable)
		InitNet();
}
void TermNet() {}
void ad_reset() {}
void tx_put(NetPacket*) {}
