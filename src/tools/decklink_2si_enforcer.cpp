/* SPDX-License-Identifier: MIT */
#include <DeckLinkAPI.h>

#include <iostream>

static bool configure_device(IDeckLink* device) {
    if (!device) {
        return false;
    }

    IDeckLinkConfiguration* config = nullptr;
    if (device->QueryInterface(IID_IDeckLinkConfiguration, reinterpret_cast<void**>(&config)) != S_OK || !config) {
        std::cerr << "[DECKLINK_2SI] Unable to query IDeckLinkConfiguration." << std::endl;
        return false;
    }

    // Configuration policy:
    // - force quad-link output topology,
    // - force 2SI mapping mode,
    // - persist settings so runtime start is deterministic across restarts.
    bool ok = true;

    if (config->SetInt(bmdDeckLinkConfigSDIOutputLinkConfiguration, bmdLinkConfigurationQuadLink) != S_OK) {
        std::cerr << "[DECKLINK_2SI] Failed to set Quad-Link SDI output." << std::endl;
        ok = false;
    }

    if (config->SetFlag(bmdDeckLinkConfigQuadLinkSDIOutputMapping, false) != S_OK) {
        std::cerr << "[DECKLINK_2SI] Failed to set 2SI mapping (false)." << std::endl;
        ok = false;
    }

    if (ok && config->WriteConfigurationToPreferences() != S_OK) {
        std::cerr << "[DECKLINK_2SI] Failed to persist DeckLink preferences (permissions?)." << std::endl;
        ok = false;
    }

    config->Release();
    return ok;
}

int main() {
    IDeckLinkIterator* iterator = CreateDeckLinkIteratorInstance();
    if (!iterator) {
        std::cerr << "[DECKLINK_2SI] Failed to create DeckLink iterator." << std::endl;
        return 1;
    }

    IDeckLink* device = nullptr;
    bool configured = false;

    // First-success strategy: configure the first device that accepts required
    // settings, then exit immediately.
    // Implication: multi-card hosts may need explicit device-selection support
    // in future if non-primary boards should be targeted.
    while (iterator->Next(&device) == S_OK) {
        if (configure_device(device)) {
            configured = true;
            device->Release();
            break;
        }

        device->Release();
        device = nullptr;
    }

    iterator->Release();

    if (!configured) {
        std::cerr << "[DECKLINK_2SI] No DeckLink device could be configured for Quad-Link 2SI." << std::endl;
        return 1;
    }

    std::cout << "[DECKLINK_2SI] DeckLink configured for Quad-Link 2SI." << std::endl;
    return 0;
}
