// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.document;
namespace {
void require(bool value) { if (!value) throw std::runtime_error("Document size boundary failed"); }
template<class Function> void too_large(Function function) {
    bool rejected {}; try { function(); } catch (const std::length_error&) { rejected = true; } require(rejected);
}
}
int main() {
    using namespace meditor;
    too_large([] { Document invalid(std::string(MAX_DOCUMENT_BYTES + 1, 'a')); });
    Document document(std::string(MAX_DOCUMENT_BYTES, 'a'));
    too_large([&] { document.apply({{MAX_DOCUMENT_BYTES, MAX_DOCUMENT_BYTES, "b"}}, 1); });
    require(document.revision() == 1 && document.size() == MAX_DOCUMENT_BYTES && !document.dirty() && !document.undo(1));
    // The last insertion is applied first internally; only the atomic final size matters.
    document.apply({{0, 1, ""}, {MAX_DOCUMENT_BYTES, MAX_DOCUMENT_BYTES, "b"}}, 1);
    require(document.size() == MAX_DOCUMENT_BYTES && document.text().ends_with('b'));
    too_large([&] { document.apply({{0, 0, "x"}, {1, 1, "y"}}, 2, "oversize"); });
    require(document.revision() == 2 && document.size() == MAX_DOCUMENT_BYTES && document.text().ends_with('b'));
    require(document.undo(2) && !document.dirty() && document.text().ends_with('a'));
    too_large([&] { document.apply({{0, 0, "x"}}, 3); });
    require(document.redo(3) && document.text().ends_with('b'));
    document.apply({{0, MAX_DOCUMENT_BYTES, "small"}}, 4);
    require(document.text() == "small");
    std::println("Document size limit validates final transactions and preserves revision/history on rejection: passed");
}
