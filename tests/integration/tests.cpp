#include "trama/trama.hpp"
#include <httplib.h>
#include <filesystem>
#include <iostream>
#include <thread>
using namespace trama;
namespace fs = std::filesystem;
static void ok(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

int main(int argc, char** argv) {
  httplib::Server rs;
  const json meta={{"schema_version","1.0.0"},{"dataset_version","0.1.0"},{"status_validacao","PRELIMINAR_NAO_HOMOLOGADO"}};
  const json caibate={{"codigo_ibge","4303301"},{"nome","Caibaté"},{"uf","RS"},{"corede_id","missoes"},{"regiao_funcional_id","RF7"},{"bioma_predominante_id","pampa"},{"biomas_presentes_ids",json::array({"mata-atlantica","pampa"})}};
  const json campina={{"codigo_ibge","4303707"},{"nome","Campina das Missões"},{"uf","RS"},{"corede_id","fronteira-noroeste"},{"regiao_funcional_id","RF7"},{"bioma_predominante_id","mata-atlantica"},{"biomas_presentes_ids",json::array({"mata-atlantica","pampa"})}};
  rs.Get("/v1/municipios/4303301",[&](const auto&,auto&z){z.set_content(json{{"data",caibate},{"meta",meta}}.dump(),"application/json");});
  rs.Get("/v1/municipios",[&](const auto&r,auto&z){auto q=r.has_param("q")?r.get_param_value("q"):"";json data=json::array();if(q=="Campina das Missões")data.push_back(campina);z.set_content(json{{"data",data},{"meta",meta}}.dump(),"application/json");});
  rs.Get("/v1/coredes",[&](const auto&,auto&z){z.set_content(json{{"data",json::array({json{{"id","missoes"},{"nome","Missões"}},json{{"id","fronteira-noroeste"},{"nome","Fronteira Noroeste"}}})},{"meta",meta}}.dump(),"application/json");});
  auto port=rs.bind_to_any_port("127.0.0.1");if(port<1){std::cerr<<"FAIL: mock TRAMA-RS bind\n";return 1;}std::thread rs_thread([&]{rs.listen_after_bind();});setenv("TRAMA_RS_URL",("http://127.0.0.1:"+std::to_string(port)).c_str(),1);
  try {
    auto tmp = fs::temp_directory_path() / "trama-public-tests-v010";
    fs::remove_all(tmp);
    auto p = paths(tmp, argc > 1 ? argv[1] : fs::current_path());
    initialize(p);
    initialize(p); // Test idempotency
    
    auto status = verify(p);
    ok(status["ok"], "database integrity");
    ok(status["journal_mode"] == "wal", "journal mode WAL");
    ok(status["migrations"] == 8, "applied 8 migrations");
    
    // 1. Territorial Context & Catalog Tests
    auto caibate_ctx = territorial_context(p, "4303301");
    ok(!caibate_ctx.is_null(), "lookup Caibaté through TRAMA-RS");
    ok(caibate_ctx["municipio"]["nome"] == "Caibaté", "Caibaté municipality name");
    ok(caibate_ctx["planejamento"]["corede"]["nome"] == "Missões", "Caibaté COREDE");
    ok(caibate_ctx["planejamento"]["regiao_funcional"]["codigo"] == "RF7", "Caibaté RF7");
    ok(caibate_ctx["ecologia"]["bioma_predominante"] == "pampa", "Caibaté example biome");
    ok(caibate_ctx["status"] == "PRELIMINAR_NAO_HOMOLOGADO", "preliminary status preserved");
    ok(caibate_ctx["dataset_version"] == "0.1.0", "TRAMA-RS dataset version preserved");

    auto campina_ctx = territorial_context(p, "Campina das Missões");
    ok(campina_ctx["planejamento"]["corede"]["nome"] == "Fronteira Noroeste", "Campina COREDE");
    ok(campina_ctx["ecologia"]["bioma_predominante"] == "mata-atlantica", "Campina example biome");

    // 2. Empty analytics contract with territorial filters
    auto empty = overview(p, {}, {}, {}, "Metropolitano Delta do Jacuí", "Região Funcional 1", "Pampa");
    ok(empty["kpis"]["reported_attendances"] == 0, "empty analytics reported attendances");
    ok(empty["kpis"]["pending_action_items"] == 0, "empty pending actions");
    ok(empty["observations_by_status"].is_array(), "observation status contract");
    ok(empty["filters"]["corede"] == "Metropolitano Delta do Jacuí", "corede filter echoed");
    ok(empty["filters"]["functional_region"] == "Região Funcional 1", "rf filter echoed");
    ok(empty["filters"]["biome"] == "Pampa", "biome filter echoed");

    // 3. Admin setup & Backup/Restore
    setup_admin(p, "admin", "Administrador", "correct-horse-battery-staple");
    auto out = tmp / "backup.sqlite3";
    backup(p, out);
    ok(fs::file_size(out) > 0, "backup size > 0");
    auto restored = paths(tmp / "restored", argc > 1 ? argv[1] : fs::current_path());
    fs::create_directories(restored.root);
    fs::copy_file(out, restored.db);
    ok(verify(restored)["ok"], "restore verified");

    rs.stop();rs_thread.join();
    std::cout << "PASS: 8 migrations, WAL, TRAMA-RS client, preliminary provenance, analytics, admin and backup/restore\n";
    fs::remove_all(tmp);
    return 0;
  } catch (const std::exception& e) {
    rs.stop();if(rs_thread.joinable())rs_thread.join();
    std::cerr << "FAIL: " << e.what() << "\n";
    return 1;
  }
}
