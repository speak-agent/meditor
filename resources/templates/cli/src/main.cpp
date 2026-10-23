// {{name}} — created by meditor (CLI template)
import std;

int main(int argc, char* argv[]) {
    std::println("Hello from {{name}}!");
    for (int i = 1; i < argc; ++i) std::println("  arg[{}] = {}", i, argv[i]);
    return 0;
}
