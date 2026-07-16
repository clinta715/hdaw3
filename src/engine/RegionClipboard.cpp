#include "RegionClipboard.h"

namespace HDAW {
namespace {
RegionClipboardEntry g_entry;
bool g_hasContent = false;
}

void RegionClipboard::store(const RegionClipboardEntry& e) {
    g_entry = e;
    g_hasContent = true;
}

const RegionClipboardEntry& RegionClipboard::get() {
    return g_entry;
}

bool RegionClipboard::hasContent() {
    return g_hasContent;
}

void RegionClipboard::clear() {
    g_entry = {};
    g_hasContent = false;
}

RegionClipboardEntry& RegionClipboard::entry() {
    return g_entry;
}
} // namespace HDAW
