// Read-only Native WLAN queries. No profile/SSID access, scanning or radio changes.
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

public static class IZ2SWlanProbe {
    [DllImport("wlanapi.dll")] static extern uint WlanOpenHandle(uint v, IntPtr r, out uint n, out IntPtr h);
    [DllImport("wlanapi.dll")] static extern uint WlanCloseHandle(IntPtr h, IntPtr r);
    [DllImport("wlanapi.dll")] static extern void WlanFreeMemory(IntPtr p);
    [DllImport("wlanapi.dll")] static extern uint WlanGetInterfaceCapability(IntPtr h, ref Guid id, IntPtr r, out IntPtr data);
    [DllImport("wlanapi.dll")] static extern uint WlanQueryInterface(IntPtr h, ref Guid id, uint op, IntPtr r, out uint size, out IntPtr data, out uint type);

    public sealed class Result {
        public bool available;
        public uint? interfaceState, operationMode;
        public bool? radioEnabled;
        public uint[] phyTypes = new uint[0];
        public List<string> errors = new List<string>();
    }

    static byte[] Query(IntPtr h, ref Guid id, uint op, Result result) {
        IntPtr p = IntPtr.Zero; uint size, type;
        try {
            uint code = WlanQueryInterface(h, ref id, op, IntPtr.Zero, out size, out p, out type);
            if (code != 0) { result.errors.Add("WlanQueryInterface(" + op + "): " + code); return null; }
            if (p == IntPtr.Zero || size < 4 || size > 65536) {
                result.errors.Add("Invalid WLAN query size"); return null;
            }
            byte[] bytes = new byte[size]; Marshal.Copy(p, bytes, 0, (int)size); return bytes;
        } finally { if (p != IntPtr.Zero) WlanFreeMemory(p); }
    }

    // Separated buffer parsers permit tests without a particular adapter/driver.
    public static uint[] ParseCapability(byte[] bytes) {
        if (bytes == null || bytes.Length < 20) throw new ArgumentException("Invalid WLAN capability record");
        uint count = BitConverter.ToUInt32(bytes, 16);
        if (count > 64 || bytes.Length < 20 + count * 4) throw new ArgumentException("Invalid WLAN PHY count");
        uint[] values = new uint[count];
        for (int i = 0; i < count; i++) values[i] = BitConverter.ToUInt32(bytes, 20 + i * 4);
        return values;
    }

    public static bool? ParseRadio(byte[] bytes) {
        if (bytes == null || bytes.Length < 4) throw new ArgumentException("Invalid WLAN radio record");
        uint count = BitConverter.ToUInt32(bytes, 0);
        if (count > 64 || bytes.Length < 4 + count * 12) throw new ArgumentException("Invalid WLAN radio count");
        bool unknown = count == 0;
        for (int i = 0; i < count; i++) {
            uint software = BitConverter.ToUInt32(bytes, 8 + i * 12), hardware = BitConverter.ToUInt32(bytes, 12 + i * 12);
            if (software == 1 && hardware == 1) return true;
            if (software != 2 && hardware != 2) unknown = true;
        }
        return unknown ? (bool?)null : false;
    }

    public static Result Inspect(string guid) {
        Result result = new Result(); IntPtr h = IntPtr.Zero, p = IntPtr.Zero; uint version;
        try {
            uint code = WlanOpenHandle(2, IntPtr.Zero, out version, out h);
            if (code != 0) { result.errors.Add("WlanOpenHandle: " + code); return result; }
            Guid id = new Guid(guid);
            byte[] state = Query(h, ref id, 6, result);
            if (state != null) { result.interfaceState = BitConverter.ToUInt32(state, 0); result.available = true; }
            byte[] mode = Query(h, ref id, 12, result);
            if (mode != null) result.operationMode = BitConverter.ToUInt32(mode, 0);
            byte[] radio = Query(h, ref id, 4, result);
            if (radio != null) result.radioEnabled = ParseRadio(radio);
            code = WlanGetInterfaceCapability(h, ref id, IntPtr.Zero, out p);
            if (code == 0 && p != IntPtr.Zero) {
                // WLAN_INTERFACE_CAPABILITY has five DWORDs and 64 PHY enums.
                byte[] capability = new byte[276]; Marshal.Copy(p, capability, 0, capability.Length);
                result.phyTypes = ParseCapability(capability);
            } else result.errors.Add("WlanGetInterfaceCapability: " + code);
        } catch (Exception error) { result.errors.Add(error.Message); }
        finally {
            if (p != IntPtr.Zero) WlanFreeMemory(p);
            if (h != IntPtr.Zero) WlanCloseHandle(h, IntPtr.Zero);
        }
        return result;
    }
}
