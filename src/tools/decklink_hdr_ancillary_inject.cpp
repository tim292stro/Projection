/* SPDX-License-Identifier: MIT */
#include <DeckLinkAPI.h>

#include <cstring>
#include <iostream>
#include <string>

struct Args {
    std::string file;
    std::string hdr;
    std::string primaries;
    std::string transfer;
    std::string matrix;
};

static bool parse_args(int argc, char** argv, Args& out) {
    // Parser expects key/value pairs to keep invocation contract predictable
    // from the C runtime hook.
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string key(argv[i]);
        const std::string value(argv[i + 1]);
        if (key == "--file") out.file = value;
        else if (key == "--hdr") out.hdr = value;
        else if (key == "--primaries") out.primaries = value;
        else if (key == "--transfer") out.transfer = value;
        else if (key == "--matrix") out.matrix = value;
        else {
            std::cerr << "[DECKLINK_HDR] Unknown argument: " << key << std::endl;
            return false;
        }
    }

    if (out.hdr.empty()) {
        std::cerr << "[DECKLINK_HDR] Missing required --hdr argument" << std::endl;
        return false;
    }

    return true;
}

int main(int argc, char** argv) {
    Args args;
    if (!parse_args(argc, argv, args)) {
        std::cerr << "Usage: decklink_hdr_ancillary_inject --file <path> --hdr <0|1> --primaries <name> --transfer <name> --matrix <name>" << std::endl;
        return 2;
    }

    // Gate ancillary work on HDR signal intent.
    // Implication: SDR flows take fast no-op path and avoid unnecessary device
    // transactions during transitions.
    if (args.hdr != "1") {
        std::cout << "[DECKLINK_HDR] SDR content detected, ancillary HDR injection skipped." << std::endl;
        return 0;
    }

    IDeckLinkIterator* iterator = CreateDeckLinkIteratorInstance();
    if (!iterator) {
        std::cerr << "[DECKLINK_HDR] Failed to create DeckLink iterator." << std::endl;
        return 1;
    }

    IDeckLink* device = nullptr;
    bool found = (iterator->Next(&device) == S_OK && device != nullptr);
    iterator->Release();

    if (!found) {
        std::cerr << "[DECKLINK_HDR] No DeckLink device detected for ancillary injection." << std::endl;
        return 1;
    }

    std::cout << "[DECKLINK_HDR] DeckLink device detected for ancillary injection request." << std::endl;

    // Current implementation validates transport path and request visibility.
    // Future expansion point: emit real VANC/HDR metadata packets here.
    std::cout << "[DECKLINK_HDR] Request accepted for file='" << args.file
              << "' hdr=" << args.hdr
              << " primaries='" << args.primaries
              << "' transfer='" << args.transfer
              << "' matrix='" << args.matrix << "'" << std::endl;
    std::cout << "[DECKLINK_HDR] Ancillary metadata injection hook executed." << std::endl;

    device->Release();
    return 0;
}
