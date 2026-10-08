#include "trama/trama.hpp"
#include <filesystem>
#include <iostream>
using namespace trama;
namespace fs = std::filesystem;
static void ok(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char** argv) {
  try {
    auto tmp = fs::temp_directory_path() / "trama-public-tests-v010";
    fs::remove_all(tmp);
    auto p = paths(tmp, argc > 1 ? argv[1] : fs::current_path());
    initialize(p);
    initialize(p);
    auto status = verify(p);
    ok(status["ok"], "integrity");
    ok(status["journal_mode"] == "wal", "WAL");
    ok(status["migrations"] == 5, "migrations");
    auto empty = overview(p);
    ok(empty["kpis"]["reported_attendances"] == 0, "empty analytics");
    ok(empty["kpis"]["pending_action_items"] == 0, "empty pending actions");
    ok(empty["observations_by_status"].is_array(), "observation status contract");
    setup_admin(p, "admin", "Administrador", "correct-horse-battery-staple");
    auto out = tmp / "backup.sqlite3";
    backup(p, out);
    ok(fs::file_size(out) > 0, "backup");
    auto restored = paths(tmp / "restored", argc > 1 ? argv[1] : fs::current_path());
    fs::create_directories(restored.root);
    fs::copy_file(out, restored.db);
    ok(verify(restored)["ok"], "restore");
    std::cout << "PASS: migrations, WAL, empty analytics, admin and backup/restore\n";
    fs::remove_all(tmp);
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
