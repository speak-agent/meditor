import std;
import meditor.document;

void require(bool value) { if (!value) throw std::runtime_error("Assertion failed"); }
int main() {
    using namespace meditor;
    Document doc("export module hello;\n你好🙂\n");
    require(doc.offset_at({1, 2}) == 27);
    require(doc.position_at(doc.offset_at({1, 4})).character == 4);
    bool rejected = false;
    try { doc.offset_at({1, 3}); } catch (...) { rejected = true; }
    require(rejected);
    const auto initial = doc.text();
    doc.apply({{0, 6, "import"}, {14, 19, "world"}}, 1);
    require(doc.text().starts_with("import module world;"));
    require(doc.undo(2) && doc.text() == initial);
    require(doc.redo(3) && doc.text().starts_with("import module world;"));
    rejected = false;
    try { doc.apply({{0, 1, "x"}}, 1); } catch (...) { rejected = true; }
    require(rejected && doc.revision() == 4);
    std::mt19937 random(42); Document generated; std::string expected;
    for (int step = 0; step < 2000; ++step) {
        auto start = random() % (expected.size() + 1);
        auto end = start + random() % (expected.size() - start + 1);
        std::string insert(random() % 7, static_cast<char>('a' + random() % 26));
        auto old = expected;
        generated.apply({{start, end, insert}}, generated.revision());
        expected.replace(start, end - start, insert);
        require(generated.text() == expected);
        require(generated.undo(generated.revision()) && generated.text() == old);
        require(generated.redo(generated.revision()) && generated.text() == expected);
    }
    Document composition("before ");
    composition.apply({{7, 7, "n"}}, composition.revision(), "ime-1");
    composition.apply({{7, 8, "ni"}}, composition.revision(), "ime-1");
    composition.apply({{7, 9, "你"}}, composition.revision(), "ime-1");
    require(composition.undo(composition.revision()) && composition.text() == "before ");
    require(composition.redo(composition.revision()) && composition.text() == "before 你");
    composition.apply({{10, 10, "!"}}, composition.revision());
    require(composition.undo(composition.revision()) && composition.text() == "before 你");
    require(!valid_utf8("\xc0\x80"));
    std::println("document transactions: passed");
}
