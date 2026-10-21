// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Sunrisepeak & mcpp-community
import std;
import meditor.document;
int main() {
    meditor::Document document("original");
    document.apply({{0, 8, "independent consumer"}}, document.revision());
    if (document.text() != "independent consumer") return 1;
    if (!document.undo(document.revision()) || document.text() != "original") return 2;
    std::println("independent document consumer: passed");
}
