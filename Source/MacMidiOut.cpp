// SPDX-License-Identifier: GPL-3.0-only
#include "MacMidiOut.h"

#if defined(__APPLE__)
#include <CoreMIDI/CoreMIDI.h>
#include <mutex>
#include <vector>

namespace sloop {

namespace {
// One CoreMIDI client for the process, and its output port (every connection shares them).
MIDIClientRef midiClient()
{
    static std::once_flag once;
    static MIDIClientRef client = 0;
    std::call_once(once, [] { MIDIClientCreate(CFSTR("SLOOP Studio"), nullptr, nullptr, &client); });
    return client;
}

MIDIPortRef outputPort()
{
    static std::once_flag once;
    static MIDIPortRef port = 0;
    std::call_once(once, [] { if (midiClient()) MIDIOutputPortCreate(midiClient(), CFSTR("SLOOP Studio out"), &port); });
    return port;
}

bool sendList(const uint8_t* bytes, size_t n, const std::function<OSStatus(const MIDIPacketList*)>& go)
{
    if (!n) return false;
    std::vector<uint8_t> mem(n + 256);   // the list header + one packet holding the whole message
    auto* list = reinterpret_cast<MIDIPacketList*>(mem.data());
    MIDIPacket* p = MIDIPacketListInit(list);
    p = MIDIPacketListAdd(list, mem.size(), p, 0, n, bytes);
    return p && go(list) == noErr;
}

std::string nameOf(MIDIEndpointRef e)
{
    CFStringRef s = nullptr;
    if (MIDIObjectGetStringProperty(e, kMIDIPropertyDisplayName, &s) != noErr || !s) return {};
    char buf[256] = {};
    CFStringGetCString(s, buf, sizeof buf, kCFStringEncodingUTF8);
    CFRelease(s);
    return buf;
}
}

std::unique_ptr<RawMidiOut> RawMidiOut::open(const std::function<bool(const std::string&)>& nameMatches)
{
    if (!outputPort()) return nullptr;
    for (ItemCount i = 0; i < MIDIGetNumberOfDestinations(); ++i)
    {
        const MIDIEndpointRef e = MIDIGetDestination(i);
        if (!nameMatches(nameOf(e))) continue;
        std::unique_ptr<RawMidiOut> r(new RawMidiOut());
        r->endpoint = e;
        SInt32 id = 0;
        MIDIObjectGetIntegerProperty(e, kMIDIPropertyUniqueID, &id);
        r->uniqueId = id;
        return r;
    }
    return nullptr;
}

RawMidiOut::~RawMidiOut() = default;

bool RawMidiOut::send(const uint8_t* bytes, size_t n)
{
    if (!endpoint) return false;
    return sendList(bytes, n, [this](const MIDIPacketList* l) { return MIDISend(outputPort(), endpoint, l); });
}

VirtualMidiSource& VirtualMidiSource::get()
{
    static VirtualMidiSource s;
    return s;
}

VirtualMidiSource::VirtualMidiSource()
{
    MIDIEndpointRef e = 0;
    if (midiClient() && MIDISourceCreate(midiClient(), CFSTR("SLOOP Studio"), &e) == noErr) source = e;
}

bool VirtualMidiSource::send(const uint8_t* bytes, size_t n)
{
    if (!source) return false;
    return sendList(bytes, n, [this](const MIDIPacketList* l) { return MIDIReceived(source, l); });
}

bool RawMidiOut::stillThere() const
{
    MIDIObjectRef obj = 0;
    MIDIObjectType type {};
    return uniqueId && MIDIObjectFindByUniqueID(uniqueId, &obj, &type) == noErr && obj == endpoint;
}

} // namespace sloop

#else

namespace sloop {
VirtualMidiSource& VirtualMidiSource::get() { static VirtualMidiSource s; return s; }
VirtualMidiSource::VirtualMidiSource() = default;
bool VirtualMidiSource::send(const uint8_t*, size_t) { return false; }
std::unique_ptr<RawMidiOut> RawMidiOut::open(const std::function<bool(const std::string&)>&) { return nullptr; }
RawMidiOut::~RawMidiOut() = default;
bool RawMidiOut::send(const uint8_t*, size_t) { return false; }
bool RawMidiOut::stillThere() const { return false; }
}

#endif
