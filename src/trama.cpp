#include "trama/trama.hpp"
#include <httplib.h>
#include <sqlite3.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <thread>
#include <unordered_map>
#include <strings.h>
#include <unistd.h>
namespace fs=std::filesystem;
namespace trama {
namespace {
struct Db { sqlite3* p{}; explicit Db(const fs::path& f){fs::create_directories(f.parent_path());if(sqlite3_open_v2(f.c_str(),&p,SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(p));exec("PRAGMA foreign_keys=ON; PRAGMA busy_timeout=5000; PRAGMA journal_mode=WAL; PRAGMA synchronous=FULL;");} ~Db(){if(p)sqlite3_close(p);} void exec(const std::string&s){char*e=nullptr;if(sqlite3_exec(p,s.c_str(),nullptr,nullptr,&e)!=SQLITE_OK){std::string m=e?e:"sqlite error";sqlite3_free(e);throw std::runtime_error(m);}} };
struct St { sqlite3_stmt* s{}; St(Db&d,const std::string&q){if(sqlite3_prepare_v2(d.p,q.c_str(),-1,&s,nullptr)!=SQLITE_OK)throw std::runtime_error(sqlite3_errmsg(d.p));} ~St(){sqlite3_finalize(s);} void b(int i,const std::string&v){sqlite3_bind_text(s,i,v.c_str(),-1,SQLITE_TRANSIENT);} void bn(int i){sqlite3_bind_null(s,i);} void bi(int i,int64_t v){sqlite3_bind_int64(s,i,v);} bool row(){int r=sqlite3_step(s);if(r==SQLITE_ROW)return true;if(r==SQLITE_DONE)return false;throw std::runtime_error(sqlite3_errmsg(sqlite3_db_handle(s)));} std::string str(int i){auto*x=sqlite3_column_text(s,i);return x?(const char*)x:"";} int64_t num(int i){return sqlite3_column_int64(s,i);} bool null(int i){return sqlite3_column_type(s,i)==SQLITE_NULL;} };
std::string now(){auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());std::tm tm{};gmtime_r(&t,&tm);std::ostringstream o;o<<std::put_time(&tm,"%FT%TZ");return o.str();}
std::string expires_in_hours(int hours){auto t=std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()+std::chrono::hours(hours));std::tm tm{};gmtime_r(&t,&tm);std::ostringstream o;o<<std::put_time(&tm,"%FT%TZ");return o.str();}
std::string hex(const unsigned char* p,size_t n){static const char*d="0123456789abcdef";std::string s(n*2,'0');for(size_t i=0;i<n;i++){s[i*2]=d[p[i]>>4];s[i*2+1]=d[p[i]&15];}return s;}
std::string random_hex(size_t n=32){std::vector<unsigned char>b(n);if(RAND_bytes(b.data(),b.size())!=1)throw std::runtime_error("random generator failed");return hex(b.data(),b.size());}
std::string sha256(std::string_view v){unsigned char out[SHA256_DIGEST_LENGTH];SHA256((const unsigned char*)v.data(),v.size(),out);return hex(out,sizeof out);}
std::string password_hash(const std::string&p){auto salt=random_hex(16);unsigned char out[32];PKCS5_PBKDF2_HMAC(p.c_str(),p.size(),(const unsigned char*)salt.data(),salt.size(),310000,EVP_sha256(),sizeof out,out);return "pbkdf2-sha256$310000$"+salt+"$"+hex(out,sizeof out);}
bool password_ok(const std::string&p,const std::string&h){auto a=h.find('$'),b=h.find('$',a+1),c=h.find('$',b+1);if(a==std::string::npos||b==std::string::npos||c==std::string::npos)return false;int it=std::stoi(h.substr(a+1,b-a-1));auto salt=h.substr(b+1,c-b-1),want=h.substr(c+1);unsigned char out[32];PKCS5_PBKDF2_HMAC(p.c_str(),p.size(),(const unsigned char*)salt.data(),salt.size(),it,EVP_sha256(),sizeof out,out);return CRYPTO_memcmp(want.data(),hex(out,sizeof out).data(),want.size())==0;}
std::string slug(std::string s){std::string o;for(unsigned char c:s){if(std::isalnum(c))o+=(char)std::tolower(c);else if(o.empty()||o.back()!='-')o+='-';}while(!o.empty()&&o.back()=='-')o.pop_back();return o;}
std::string read(const fs::path&p){std::ifstream f(p,std::ios::binary);if(!f)throw std::runtime_error("cannot read "+p.string());return {(std::istreambuf_iterator<char>(f)),{}};}
void bind_opt(St&q,int i,const std::optional<std::string>&v){if(v)q.b(i,*v);else q.bn(i);}
void bind_number_or_null(St&q,int i,const json&j,const char*key){if(j.contains(key)&&!j[key].is_null()&&j[key]!="")sqlite3_bind_double(q.s,i,j[key].is_number()?j[key].get<double>():std::stod(j[key].get<std::string>()));else q.bn(i);}
std::string territory(Db&d,const std::string&kind,const std::string&name,const std::optional<std::string>&parent,const std::string&ts,const std::optional<std::string>&code={}){if(name.empty())return parent.value_or("");auto id="territory-"+sha256(kind+"\n"+name+"\n"+parent.value_or("")).substr(0,24);St s(d,"INSERT INTO territories(id,parent_id,kind,code,name,created_at,updated_at) VALUES(?,?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET parent_id=excluded.parent_id,code=COALESCE(excluded.code,territories.code),name=excluded.name,updated_at=excluded.updated_at");s.b(1,id);bind_opt(s,2,parent);s.b(3,kind);bind_opt(s,4,code);s.b(5,name);s.b(6,ts);s.b(7,ts);s.row();return id;}
struct TerritoryMatch { std::string ibge_code,municipality_name,uf,corede_code,corede_name,rf_code,rf_name,biome_predominant,biomes_occurring,dataset_version,source_planning,source_ecology,status; };
struct RsEndpoint { std::string host{"10.163.80.176"}; int port{8080}; };
RsEndpoint rs_endpoint(){
  std::string url=std::getenv("TRAMA_RS_URL")?std::getenv("TRAMA_RS_URL"):"http://10.163.80.176:8080";
  constexpr std::string_view prefix="http://";
  if(!url.starts_with(prefix))throw std::runtime_error("TRAMA_RS_URL deve usar http:// nesta versão");
  auto authority=url.substr(prefix.size());if(auto slash=authority.find('/');slash!=std::string::npos)authority.resize(slash);
  RsEndpoint out;auto colon=authority.rfind(':');if(colon==std::string::npos)out.host=authority;else{out.host=authority.substr(0,colon);out.port=std::stoi(authority.substr(colon+1));}
  if(out.host.empty()||out.port<1||out.port>65535)throw std::runtime_error("TRAMA_RS_URL inválida");
  return out;
}
json rs_get(const std::string& path){auto e=rs_endpoint();httplib::Client client(e.host,e.port);client.set_connection_timeout(2,0);client.set_read_timeout(5,0);auto response=client.Get(path);if(!response)throw std::runtime_error("TRAMA-RS indisponível em "+e.host+":"+std::to_string(e.port));if(response->status!=200)throw std::runtime_error("TRAMA-RS respondeu HTTP "+std::to_string(response->status));return json::parse(response->body);}
std::unordered_map<std::string,std::string> rs_corede_names(){std::unordered_map<std::string,std::string> out;for(auto&x:rs_get("/v1/coredes").at("data"))out[x.at("id")]=x.at("nome");return out;}
TerritoryMatch rs_match(const json&m,const json&meta,const std::unordered_map<std::string,std::string>&names){
  auto nullable=[](const json&x,const char*k){return x.contains(k)&&!x[k].is_null()?x[k].get<std::string>():std::string{};};
  auto rf=nullable(m,"regiao_funcional_id"),corede=nullable(m,"corede_id");json occurring=m.contains("biomas_presentes_ids")?m["biomas_presentes_ids"]:json(nullptr);
  return{nullable(m,"codigo_ibge"),m.at("nome"),m.value("uf","RS"),corede,names.contains(corede)?names.at(corede):corede,rf,rf.empty()?"":("Região Funcional "+rf.substr(2)),nullable(m,"bioma_predominante_id"),occurring.is_null()?"":occurring.dump(),meta.value("dataset_version","unknown"),"TRAMA-RS "+meta.value("status_validacao","DESCONHECIDO"),"TRAMA-RS; classificação ecológica conforme disponibilidade declarada",meta.value("status_validacao","DESCONHECIDO")};
}
std::optional<TerritoryMatch> lookup_catalog(Db&,const std::string&q){
  if(q.empty())return{};
  json response;
  if(q.size()==7&&std::ranges::all_of(q,[](unsigned char c){return std::isdigit(c);}))response=rs_get("/v1/municipios/"+q);
  else response=rs_get("/v1/municipios?q="+httplib::encode_uri_component(q)+"&limit=500");
  auto names=rs_corede_names();if(response["data"].is_object())return rs_match(response["data"],response["meta"],names);
  for(auto&m:response["data"]){auto name=m.value("nome","");if(strcasecmp(name.c_str(),q.c_str())==0)return rs_match(m,response["meta"],names);}return{};
}
json legacy_territory(const TerritoryMatch&m){return{{"ibge_code",m.ibge_code.empty()?json(nullptr):json(m.ibge_code)},{"municipality_name",m.municipality_name},{"uf",m.uf},{"corede_code",m.corede_code},{"corede_name",m.corede_name},{"rf_code",m.rf_code},{"rf_name",m.rf_name},{"biome_predominant",m.biome_predominant.empty()?json(nullptr):json(m.biome_predominant)},{"biomes_occurring",m.biomes_occurring.empty()?json(nullptr):json::parse(m.biomes_occurring)},{"dataset_version",m.dataset_version},{"source_planning",m.source_planning},{"source_ecology",m.source_ecology},{"status_validacao",m.status}};}
std::string territory_chain(Db&d,json&j,const std::string&ts){
  auto lookup_term=j.value("ibge_code","");
  if(lookup_term.empty())lookup_term=j.value("municipality","");
  auto match=lookup_catalog(d,lookup_term);
  if(match){
    j["ibge_code"]=match->ibge_code;
    j["municipality"]=match->municipality_name;
    if(!j.contains("corede")||j["corede"].empty()||j["corede"].is_null())j["corede"]=match->corede_name;
    if(!j.contains("functional_region")||j["functional_region"].empty()||j["functional_region"].is_null())j["functional_region"]=match->rf_name;
    if(!j.contains("biome_predominant")||j["biome_predominant"].empty()||j["biome_predominant"].is_null())j["biome_predominant"]=match->biome_predominant;
    if(!j.contains("biomes_occurring")||j["biomes_occurring"].empty()||j["biomes_occurring"].is_null())j["biomes_occurring"]=match->biomes_occurring;
  }
  std::optional<std::string> rf_id;
  auto rf=j.value("functional_region","");
  if(!rf.empty())rf_id=territory(d,"functional_region",rf,{},ts);
  std::optional<std::string> corede_id;
  auto corede=j.value("corede","");
  if(!corede.empty())corede_id=territory(d,"corede",corede,rf_id,ts);
  auto municipality=j.value("municipality","");
  if(municipality.empty())throw std::runtime_error("município é obrigatório");
  auto ibge=j.value("ibge_code","");
  return territory(d,"municipality",municipality,corede_id,ts,ibge.empty()?std::optional<std::string>{}:std::optional<std::string>{ibge});
}
std::string arg(int argc,char**argv,const std::string&name,const std::string&def=""){for(int i=1;i+1<argc;i++)if(argv[i]==name)return argv[i+1];return def;}
std::string scalar(Db&d,const std::string&q){St s(d,q);return s.row()?s.str(0):"";}
void audit(Db&d,const std::string&actor,const std::string&project,const std::string&type,const std::string&id,const std::string&action,const json&after){std::string prev=scalar(d,"SELECT event_hash FROM audit_events ORDER BY rowid DESC LIMIT 1"),at=now(),payload=prev+actor+project+type+id+action+at+after.dump();St s(d,"INSERT INTO audit_events VALUES(?,?,?,?,?,?,?,?,?,?,?,?)");s.b(1,"audit-"+random_hex(12));bind_opt(s,2,actor.empty()?std::optional<std::string>{}:actor);bind_opt(s,3,project.empty()?std::optional<std::string>{}:project);s.b(4,type);s.b(5,id);s.b(6,action);s.b(7,at);s.bn(8);s.b(9,after.dump());s.bn(10);s.b(11,prev);s.b(12,sha256(payload));s.row();}
void migrate(Db&d,const Paths&p){d.exec("CREATE TABLE IF NOT EXISTS schema_migrations(version INTEGER PRIMARY KEY,name TEXT NOT NULL,checksum TEXT NOT NULL,applied_at TEXT NOT NULL)");std::vector<fs::path> files; for(auto const& e:fs::directory_iterator(p.migrations)) files.push_back(e.path()); std::sort(files.begin(),files.end()); for(auto const& path:files){ fs::directory_entry e(path);if(e.path().extension()!=".sql")continue;auto name=e.path().filename().string();int ver=std::stoi(name.substr(0,3));auto sql=read(e.path()),sum=sha256(sql);St c(d,"SELECT checksum FROM schema_migrations WHERE version=?");c.bi(1,ver);if(c.row()){if(c.str(0)!=sum)throw std::runtime_error("migration checksum mismatch: "+name);continue;}d.exec("BEGIN IMMEDIATE");try{d.exec(sql);St i(d,"INSERT INTO schema_migrations VALUES(?,?,?,?)");i.bi(1,ver);i.b(2,name);i.b(3,sum);i.b(4,now());i.row();d.exec("COMMIT");}catch(...){d.exec("ROLLBACK");throw;}}}
json rows(Db&d,const std::string&q,const std::function<void(St&)>&bind={}){St s(d,q);if(bind)bind(s);json a=json::array();int n=sqlite3_column_count(s.s);while(s.row()){json x;for(int i=0;i<n;i++){auto k=sqlite3_column_name(s.s,i);if(s.null(i))x[k]=nullptr;else if(sqlite3_column_type(s.s,i)==SQLITE_INTEGER)x[k]=s.num(i);else x[k]=s.str(i);}a.push_back(x);}return a;}
struct User {std::string id,login,role,csrf;};
std::optional<User> auth(Db&d,const httplib::Request&r){auto c=r.get_header_value("Cookie");auto pos=c.find("trama_session=");if(pos==std::string::npos)return{};auto token=c.substr(pos+14);if(auto e=token.find(';');e!=std::string::npos)token.resize(e);St s(d,"SELECT u.id,u.login,u.system_role,se.csrf_hash FROM sessions se JOIN users u ON u.id=se.user_id WHERE se.secret_hash=? AND se.revoked_at IS NULL AND se.expires_at>strftime('%Y-%m-%dT%H:%M:%SZ','now') AND u.active=1");s.b(1,sha256(token));if(!s.row())return{};return User{s.str(0),s.str(1),s.str(2),s.str(3)};}
void headers(httplib::Response&r){r.set_header("X-Content-Type-Options","nosniff");r.set_header("X-Frame-Options","SAMEORIGIN");r.set_header("Referrer-Policy","no-referrer");r.set_header("Content-Security-Policy","default-src 'self' 'unsafe-inline' https://unpkg.com https://*.tile.openstreetmap.org; style-src 'self' 'unsafe-inline' https://unpkg.com; script-src 'self' 'unsafe-inline' https://unpkg.com; img-src 'self' data: https: blob:; frame-ancestors 'self'");}
void respond(httplib::Response&r,const json&j,int status=200){r.status=status;r.set_content(j.dump(),"application/json; charset=utf-8");headers(r);}
void error(httplib::Response&r,int status,const std::string&code,const std::string&msg){respond(r,{{"error",{{"code",code},{"message",msg},{"details",json::object()}}},{"request_id",random_hex(8)}},status);}
bool csrf(const httplib::Request&r,const User&u){return sha256(r.get_header_value("X-CSRF-Token"))==u.csrf;}
std::optional<User> require(Db&d,const httplib::Request&r,httplib::Response&z,bool write=false,bool admin=false){auto u=auth(d,r);if(!u){error(z,401,"unauthenticated","Autenticação necessária");return{};}if((admin||write)&&u->role!="admin"){error(z,403,"forbidden","Permissão de escrita insuficiente");return{};}if(write&&!csrf(r,*u)){error(z,403,"csrf_failed","Token CSRF inválido");return{};}return u;}
std::string esc(std::string s){std::string o;for(char c:s){if(c=='&')o+="&amp;";else if(c=='<')o+="&lt;";else if(c=='>')o+="&gt;";else if(c=='\"')o+="&quot;";else o+=c;}return o;}
}
Paths paths(const fs::path&root,const fs::path&source){auto base=source.empty()?fs::current_path():source;return{root,root/"trama.sqlite3",root/"evidence",base/"migrations",base/"web"/"dist"};}
void initialize(const Paths&p){fs::create_directories(p.root);fs::create_directories(p.evidence);Db d(p.db);migrate(d,p);}
json sync_territories(const Paths&p){
  auto source=rs_get("/v1/municipios?limit=500");auto names=rs_corede_names();Db d(p.db);auto units=rows(d,"SELECT u.id,m.id municipality_id,m.name municipality_name FROM units u JOIN territories m ON m.id=u.territory_id WHERE u.deleted_at IS NULL ORDER BY u.id");int updated=0,unmatched=0;json missing=json::array();d.exec("BEGIN IMMEDIATE");try{
    for(auto&u:units){const json*found=nullptr;for(auto&m:source["data"]){auto name=m.value("nome","");auto wanted=u["municipality_name"].get<std::string>();if(strcasecmp(name.c_str(),wanted.c_str())==0){found=&m;break;}}if(!found){++unmatched;missing.push_back(u["municipality_name"]);continue;}auto match=rs_match(*found,source["meta"],names);auto ts=now();std::optional<std::string> rf_id;if(!match.rf_name.empty())rf_id=territory(d,"functional_region",match.rf_name,{},ts,match.rf_code);std::optional<std::string> corede_id;if(!match.corede_name.empty())corede_id=territory(d,"corede",match.corede_name,rf_id,ts,match.corede_code);St municipality(d,"UPDATE territories SET parent_id=?,code=?,name=?,updated_at=? WHERE id=?");bind_opt(municipality,1,corede_id);bind_opt(municipality,2,match.ibge_code.empty()?std::optional<std::string>{}:std::optional<std::string>{match.ibge_code});municipality.b(3,match.municipality_name);municipality.b(4,ts);municipality.b(5,u["municipality_id"]);municipality.row();St unit(d,"UPDATE units SET ibge_code=?,biome_predominant=?,biomes_occurring=?,updated_at=?,revision=revision+1 WHERE id=?");bind_opt(unit,1,match.ibge_code.empty()?std::optional<std::string>{}:std::optional<std::string>{match.ibge_code});bind_opt(unit,2,match.biome_predominant.empty()?std::optional<std::string>{}:std::optional<std::string>{match.biome_predominant});bind_opt(unit,3,match.biomes_occurring.empty()?std::optional<std::string>{}:std::optional<std::string>{match.biomes_occurring});unit.b(4,ts);unit.b(5,u["id"]);unit.row();++updated;}
    St state(d,"UPDATE integration_state SET endpoint=?,status=?,checked_at=?,dataset_version=?,validation_status=?,details_json=? WHERE integration_id='trama-rs'");auto endpoint=rs_endpoint();state.b(1,"http://"+endpoint.host+":"+std::to_string(endpoint.port));state.b(2,"synchronized");state.b(3,now());state.b(4,source["meta"].value("dataset_version","unknown"));state.b(5,source["meta"].value("status_validacao","DESCONHECIDO"));state.b(6,json{{"updated",updated},{"unmatched",unmatched},{"unmatched_names",missing}}.dump());state.row();d.exec("COMMIT");
  }catch(...){d.exec("ROLLBACK");throw;}return{{"updated",updated},{"unmatched",unmatched},{"unmatched_names",missing},{"dataset_version",source["meta"].value("dataset_version","unknown")},{"status_validacao",source["meta"].value("status_validacao","DESCONHECIDO")}};
}
json territorial_context(const Paths&p,const std::string&ibge_or_name){
  Db d(p.db);
  auto match=lookup_catalog(d,ibge_or_name);
  if(!match)return json(nullptr);
  json occurring=json::array();
  if(!match->biomes_occurring.empty())try{occurring=json::parse(match->biomes_occurring);}catch(...){occurring=nullptr;}else occurring=nullptr;
  return{
    {"municipio",{{"codigo_ibge",match->ibge_code.empty()?json(nullptr):json(match->ibge_code)},{"nome",match->municipality_name},{"uf",match->uf}}},
    {"planejamento",{{"corede",{{"codigo",match->corede_code},{"nome",match->corede_name}}},{"regiao_funcional",{{"codigo",match->rf_code},{"nome",match->rf_name}}}}},
    {"ecologia",{{"bioma_predominante",match->biome_predominant.empty()?json(nullptr):json(match->biome_predominant)},{"biomas_ocorrentes",occurring},{"criterio",match->biome_predominant.empty()?json(nullptr):json("predominancia_por_area")}}},
    {"referencias",{{"planejamento",match->source_planning},{"ecologia",match->source_ecology}}},
    {"dataset_version",match->dataset_version},
    {"status",match->status}
  };
}
json overview(const Paths&p,std::optional<std::string>group,std::optional<std::string>unit,std::optional<std::string>project,std::optional<std::string>corede,std::optional<std::string>functional_region,std::optional<std::string>biome){
  Db d(p.db);
  std::string w=" WHERE project_id=?",q=project.value_or(scalar(d,"SELECT id FROM projects WHERE status != 'archived' ORDER BY created_at LIMIT 1"));
  std::vector<std::string>params={q};
  if(group&&!group->empty()){w+=" AND project_group_id=?";params.push_back(*group);}
  if(unit&&!unit->empty()){w+=" AND unit_id=?";params.push_back(*unit);}
  if(corede&&!corede->empty()){w+=" AND corede=?";params.push_back(*corede);}
  if(functional_region&&!functional_region->empty()){w+=" AND functional_region=?";params.push_back(*functional_region);}
  if(biome&&!biome->empty()){w+=" AND (biome_predominant=? OR biomes_occurring LIKE ?)";params.push_back(*biome);params.push_back("%"+*biome+"%");}
  St s(d,"SELECT count(*),coalesce(sum(total),0),coalesce(sum(women),0),coalesce(sum(men),0),coalesce(sum(youth),0) FROM v_attendance"+w);
  for(size_t i=0;i<params.size();++i)s.b(i+1,params[i]);
  s.row();
  auto total=s.num(1);
  St pending(d,"SELECT count(*) FROM action_items WHERE project_id=? AND deleted_at IS NULL AND status IN ('open','in_progress','blocked')");
  pending.b(1,q);
  pending.row();
  auto observation_status=rows(d,"SELECT validation_status status,count(*) count FROM observations o JOIN activities a ON a.id=o.activity_id WHERE a.project_id=? AND o.deleted_at IS NULL GROUP BY validation_status",[&](St&x){x.b(1,q);});
  json out;
  out["project_id"]=q;
  out["filters"]={
    {"group_id",group?json(*group):json(nullptr)},
    {"unit_id",unit?json(*unit):json(nullptr)},
    {"corede",corede?json(*corede):json(nullptr)},
    {"functional_region",functional_region?json(*functional_region):json(nullptr)},
    {"biome",biome?json(*biome):json(nullptr)}
  };
  out["kpis"]={
    {"uacs_documented",s.num(0)},
    {"reported_attendances",total},
    {"reported_women",s.num(2)},
    {"reported_men",s.num(3)},
    {"reported_youth",s.num(4)},
    {"female_share",total?json((double)s.num(2)*100/total):json(nullptr)},
    {"youth_share",total?json((double)s.num(4)*100/total):json(nullptr)},
    {"pending_action_items",pending.num(0)}
  };
  out["observations_by_status"]=observation_status;
  out["rule_version"]="attendance-v1";
  out["generated_at"]=now();
  return out;
}
void setup_admin(const Paths&p,const std::string&login,const std::string&display,const std::string&password){if(password.size()<12)throw std::runtime_error("password must have at least 12 characters");initialize(p);Db d(p.db);if(std::stoll(scalar(d,"SELECT count(*) FROM users WHERE system_role='admin' AND active=1"))>0)throw std::runtime_error("an active administrator already exists");St s(d,"INSERT INTO users VALUES(?,?,?,?,?,?,?,?)");auto id="user-"+random_hex(12),ts=now();s.b(1,id);s.b(2,login);s.b(3,display);s.b(4,password_hash(password));s.b(5,"admin");s.bi(6,1);s.b(7,ts);s.b(8,ts);s.row();audit(d,id,"","user",id,"bootstrap_admin",{{"login",login}});}
void backup(const Paths&p,const fs::path&out){Db src(p.db),dst(out);sqlite3_backup*b=sqlite3_backup_init(dst.p,"main",src.p,"main");if(!b)throw std::runtime_error(sqlite3_errmsg(dst.p));int rc=sqlite3_backup_step(b,-1);sqlite3_backup_finish(b);if(rc!=SQLITE_DONE)throw std::runtime_error("backup failed");}
json verify(const Paths&p){Db d(p.db);auto integrity=scalar(d,"PRAGMA integrity_check");auto fk=rows(d,"PRAGMA foreign_key_check");return{{"integrity",integrity},{"foreign_key_violations",fk.size()},{"journal_mode",scalar(d,"PRAGMA journal_mode")},{"migrations",std::stoll(scalar(d,"SELECT count(*) FROM schema_migrations"))},{"ok",integrity=="ok"&&fk.empty()}};}
int cli(int argc,char**argv){if(argc<2){std::cout<<"TRAMA 0.1.0\nUso: trama <init|setup-admin|doctor|verify|backup|restore|version> [opções]\n";return 2;}std::string cmd=argv[1],root=arg(argc,argv,"--data-dir","var"),source=arg(argc,argv,"--source-dir",fs::current_path().string());auto p=paths(root,source);if(cmd=="version"){std::cout<<"TRAMA 0.1.0 (C++ "<<__cplusplus<<", SQLite "<<sqlite3_libversion()<<")\n";return 0;}if(cmd=="init"){initialize(p);std::cout<<"Banco inicializado: "<<p.db<<"\n";}else if(cmd=="setup-admin"){std::string login=arg(argc,argv,"--login","admin"),display=arg(argc,argv,"--display-name","Administrador"),pass=arg(argc,argv,"--password");if(pass.empty()){if(!isatty(STDIN_FILENO))throw std::runtime_error("password prompt requires a terminal");char*raw=getpass("Senha (mínimo 12 caracteres): ");pass=raw?raw:"";}setup_admin(p,login,display,pass);std::cout<<"Administrador criado.\n";}else if(cmd=="verify"||cmd=="doctor"){auto j=verify(p);j["compiler"]="GCC "+std::to_string(__GNUC__)+"."+std::to_string(__GNUC_MINOR__);j["mode"]="lan-ready";j["lan_ready"]=true;j["lan_note"]="Escuta em 0.0.0.0 habilitada";std::cout<<j.dump(2)<<"\n";return j["ok"]?0:1;}else if(cmd=="backup"){auto out=arg(argc,argv,"--output");if(out.empty())throw std::runtime_error("--output is required");backup(p,out);std::cout<<"Backup criado: "<<out<<"\n";}else if(cmd=="restore"){auto in=arg(argc,argv,"--input");if(in.empty())throw std::runtime_error("--input is required");fs::create_directories(p.root);Db src(in),dst(p.db);sqlite3_backup*b=sqlite3_backup_init(dst.p,"main",src.p,"main");if(!b)throw std::runtime_error(sqlite3_errmsg(dst.p));int rc=sqlite3_backup_step(b,-1);sqlite3_backup_finish(b);if(rc!=SQLITE_DONE)throw std::runtime_error("restore failed");std::cout<<"Restauração concluída.\n";}else throw std::runtime_error("unknown command: "+cmd);return 0;}

int server_main(int argc,char**argv){std::string root=arg(argc,argv,"--data-dir","var"),source=arg(argc,argv,"--source-dir",fs::current_path().string()),host=arg(argc,argv,"--host","0.0.0.0");int port=std::stoi(arg(argc,argv,"--port","8088"));auto p=paths(root,source);initialize(p);try{auto sync=sync_territories(p);std::cout<<"TRAMA-RS sincronizado: "<<sync["updated"]<<" unidades atualizadas, "<<sync["unmatched"]<<" não conciliadas\n";}catch(const std::exception&e){std::cerr<<"Aviso: sincronização TRAMA-RS indisponível: "<<e.what()<<"\n";}Db check(p.db);if(!fs::exists(p.web/"index.html"))throw std::runtime_error("frontend ausente; execute ./scripts/build.sh");httplib::Server app;app.set_payload_max_length(20*1024*1024);app.set_read_timeout(15,0);app.set_write_timeout(30,0);app.set_error_handler([](const httplib::Request&,httplib::Response&r){if(r.status>=400&&r.get_header_value("Content-Type").empty())error(r,r.status,"http_error","Requisição não atendida");});app.Get("/healthz",[](const auto&,auto&r){respond(r,{{"status","ok"},{"service","trama"}});});app.Get("/readyz",[p](const auto&,auto&r){try{auto v=verify(p);if(!v["ok"].get<bool>())return error(r,503,"not_ready","Banco não íntegro");respond(r,{{"status","ready"},{"database","ok"},{"static_files",fs::exists(p.web/"index.html")}});}catch(...){error(r,503,"not_ready","Serviço indisponível");}});
 app.Post("/api/v1/auth/login",[p](const httplib::Request&r,httplib::Response&z){try{auto j=json::parse(r.body);Db d(p.db);St s(d,"SELECT id,login,display_name,password_hash,system_role FROM users WHERE login=? AND active=1");s.b(1,j.value("login",""));if(!s.row()||!password_ok(j.value("password",""),s.str(3))){std::this_thread::sleep_for(std::chrono::milliseconds(150));return error(z,401,"invalid_credentials","Credenciais inválidas");}auto token=random_hex(),csrf_token=random_hex(),id="session-"+random_hex(12);St i(d,"INSERT INTO sessions VALUES(?,?,?,?,?,?,?)");i.b(1,id);i.b(2,s.str(0));i.b(3,sha256(token));i.b(4,sha256(csrf_token));i.b(5,expires_in_hours(8));i.bn(6);i.b(7,now());i.row();z.set_header("Set-Cookie","trama_session="+token+"; Path=/; HttpOnly; SameSite=Strict");respond(z,{{"user",{{"id",s.str(0)},{"login",s.str(1)},{"display_name",s.str(2)},{"system_role",s.str(4)}}},{"csrf_token",csrf_token}});}catch(...){error(z,400,"invalid_json","Corpo inválido");}});
 app.Post("/api/v1/auth/logout",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);auto u=require(d,r,z,true);if(!u)return;std::string c=r.get_header_value("Cookie");size_t pos=c.find("trama_session=");std::string token=c.substr(pos+14);if(auto e=token.find(';');e!=std::string::npos)token.resize(e);St s(d,"UPDATE sessions SET revoked_at=? WHERE secret_hash=?");s.b(1,now());s.b(2,sha256(token));s.row();z.set_header("Set-Cookie","trama_session=; Path=/; Max-Age=0; HttpOnly; SameSite=Strict");respond(z,{{"ok",true}});});
 app.Get("/api/v1/auth/me",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);auto u=require(d,r,z);if(!u)return;respond(z,{{"user",{{"id",u->id},{"login",u->login},{"system_role",u->role}}}});});
 app.Get("/api/v1/projects",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;respond(z,{{"data",rows(d,"SELECT id,code,name,description,status,period_start,period_end,revision FROM projects WHERE deleted_at IS NULL ORDER BY name")}});});
 app.Post("/api/v1/projects",[p](const httplib::Request&r,httplib::Response&z){try{Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto j=json::parse(r.body);if(j.value("code","").empty()||j.value("name","").empty())return error(z,422,"validation_error","Código e nome são obrigatórios");auto id="project-"+random_hex(12),ts=now();St s(d,"INSERT INTO projects(id,code,name,description,status,period_start,period_end,created_at,updated_at,revision) VALUES(?,?,?,?,?,?,?,?,?,1)");s.b(1,id);s.b(2,j["code"]);s.b(3,j["name"]);s.b(4,j.value("description",""));s.b(5,j.value("status","active"));bind_opt(s,6,j.contains("period_start")&&!j["period_start"].is_null()?std::optional<std::string>(j["period_start"]):std::nullopt);bind_opt(s,7,j.contains("period_end")&&!j["period_end"].is_null()?std::optional<std::string>(j["period_end"]):std::nullopt);s.b(8,ts);s.b(9,ts);s.row();audit(d,u->id,id,"project",id,"create",j);respond(z,{{"data",{{"id",id},{"revision",1}}}},201);}catch(const std::exception&e){error(z,422,"validation_error",e.what());}});
 app.Get(R"(/api/v1/projects/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;auto a=rows(d,"SELECT id,code,name,description,status,period_start,period_end,created_at,updated_at,revision FROM projects WHERE id=? AND deleted_at IS NULL",[&](St&s){s.b(1,r.matches[1]);});if(a.empty())return error(z,404,"not_found","Projeto não encontrado");respond(z,{{"data",a[0]}});});
 app.Patch(R"(/api/v1/projects/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){try{Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto j=json::parse(r.body);auto id=r.matches[1].str();int rev=j.value("revision",0);St old(d,"SELECT code,name,description,status,revision FROM projects WHERE id=? AND deleted_at IS NULL");old.b(1,id);if(!old.row())return error(z,404,"not_found","Projeto não encontrado");if(rev!=old.num(4))return error(z,409,"revision_conflict","Revisão desatualizada");St s(d,"UPDATE projects SET code=?,name=?,description=?,status=?,updated_at=?,revision=revision+1 WHERE id=? AND revision=?");s.b(1,j.value("code",old.str(0)));s.b(2,j.value("name",old.str(1)));s.b(3,j.value("description",old.str(2)));s.b(4,j.value("status",old.str(3)));s.b(5,now());s.b(6,id);s.bi(7,rev);s.row();audit(d,u->id,id,"project",id,"update",j);respond(z,{{"data",{{"id",id},{"revision",rev+1}}}});}catch(const std::exception&e){error(z,422,"validation_error",e.what());}});
 app.Delete(R"(/api/v1/projects/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto id=r.matches[1].str();St s(d,"UPDATE projects SET status='archived',deleted_at=?,updated_at=?,revision=revision+1 WHERE id=? AND deleted_at IS NULL");s.b(1,now());s.b(2,now());s.b(3,id);s.row();if(sqlite3_changes(d.p)==0)return error(z,404,"not_found","Projeto não encontrado");audit(d,u->id,id,"project",id,"delete",json::object());respond(z,{{"ok",true}});});
 app.Get("/api/v1/project-groups",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;respond(z,{{"data",rows(d,"SELECT id,project_id,code,label,external_reference FROM project_groups WHERE project_id=? ORDER BY code",[&](St&s){s.b(1,r.get_param_value("project_id"));})}});});
 app.Get("/api/v1/territories",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;respond(z,{{"data",rows(d,"SELECT id,kind,code,name FROM territories ORDER BY name")}});});
 app.Get("/api/v1/territories/catalog",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);if(!require(d,r,z))return;if(r.has_param("biome")&&!r.get_param_value("biome").empty())return error(z,409,"dados_bioma_incompletos","O TRAMA-RS ainda não possui cobertura municipal completa de biomas");
   try{std::string path="/v1/municipios?limit=500";if(r.has_param("q")&&!r.get_param_value("q").empty())path+="&q="+httplib::encode_uri_component(r.get_param_value("q"));if(r.has_param("corede")&&!r.get_param_value("corede").empty())path+="&corede_id="+httplib::encode_uri_component(r.get_param_value("corede"));if(r.has_param("functional_region")&&!r.get_param_value("functional_region").empty())path+="&regiao_funcional_id="+httplib::encode_uri_component(r.get_param_value("functional_region"));auto source=rs_get(path);auto names=rs_corede_names();json data=json::array();for(auto&m:source["data"])data.push_back(legacy_territory(rs_match(m,source["meta"],names)));respond(z,{{"data",data},{"meta",source["meta"]}});}catch(const std::exception&e){error(z,503,"trama_rs_indisponivel",e.what());}
 });
 app.Get("/api/v1/territories/dimensions",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);if(!require(d,r,z))return;try{auto cs=rs_get("/v1/coredes"),rs=rs_get("/v1/regioes-funcionais"),bs=rs_get("/v1/biomas");json coredes=json::array(),rfs=json::array(),biomes=json::array();for(auto&x:cs["data"])coredes.push_back({{"name",x["nome"]},{"code",x["id"]}});for(auto&x:rs["data"])rfs.push_back({{"name","Região Funcional "+std::to_string(x["numero"].get<int>())},{"code",x["id"]}});for(auto&x:bs["data"])biomes.push_back({{"name",x["nome"]},{"code",x["id"]},{"available",false}});respond(z,{{"data",{{"coredes",coredes},{"functional_regions",rfs},{"biomes",biomes}}},{"meta",{{"source","TRAMA-RS"},{"status_validacao",cs["meta"]["status_validacao"]},{"biome_filters_available",false}}}});}catch(const std::exception&e){error(z,503,"trama_rs_indisponivel",e.what());}
 });
 app.Get(R"(/api/v1/maps/(municipios|coredes|regioes-funcionais|biomas))",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;try{auto geo=rs_get("/v1/mapa/"+r.matches[1].str()+".geojson");z.set_content(geo.dump(),"application/geo+json; charset=utf-8");headers(z);}catch(const std::exception&e){error(z,503,"mapa_indisponivel",e.what());}});
 app.Get(R"(/api/v1/territories/context/([0-9]{7}))",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);if(!require(d,r,z))return;
   auto ctx=territorial_context(p,r.matches[1].str());
   if(ctx.is_null())return error(z,404,"not_found","Município não encontrado no catálogo territorial");
   respond(z,{{"data",ctx}});
 });
 app.Get("/api/v1/territories/lookup",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);if(!require(d,r,z))return;
   std::string term=r.has_param("ibge")?r.get_param_value("ibge"):r.get_param_value("q");
   if(term.empty()&&r.has_param("name"))term=r.get_param_value("name");
   auto ctx=territorial_context(p,term);
   if(ctx.is_null())return error(z,404,"not_found","Município não encontrado no catálogo territorial");
   respond(z,{{"data",ctx}});
 });
 app.Get("/api/v1/units",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);if(!require(d,r,z))return;
   respond(z,{{"data",rows(d,"SELECT u.id,u.project_id,u.project_group_id,u.code,u.name,u.unit_type,u.status,u.revision,u.latitude,u.longitude,u.ibge_code,m.name municipality,c.name corede,rf.name functional_region,COALESCE(u.biome_predominant,b.name) biome,u.biome_predominant,u.biomes_occurring FROM units u JOIN territories m ON m.id=u.territory_id LEFT JOIN territories c ON c.id=m.parent_id AND c.kind='corede' LEFT JOIN territories rf ON rf.id=c.parent_id AND rf.kind='functional_region' LEFT JOIN territories b ON b.id=rf.parent_id AND b.kind='biome' WHERE u.project_id=? AND u.deleted_at IS NULL ORDER BY u.name",[&](St&s){s.b(1,r.get_param_value("project_id"));})}});
 });
 app.Post("/api/v1/units",[p](const httplib::Request&r,httplib::Response&z){
   try{
     Db d(p.db);auto u=require(d,r,z,true);if(!u)return;
     auto j=json::parse(r.body);
     if(j.value("project_id","").empty()||j.value("code","").empty()||j.value("name","").empty()||(j.value("municipality","").empty()&&j.value("ibge_code","").empty()))return error(z,422,"validation_error","Projeto, código, nome e município são obrigatórios");
     auto id="unit-"+random_hex(12),ts=now();
     d.exec("BEGIN IMMEDIATE");
     try{
       auto tid=territory_chain(d,j,ts);
       St s(d,"INSERT INTO units(id,project_id,territory_id,project_group_id,code,name,unit_type,status,created_at,updated_at,revision,latitude,longitude,ibge_code,biome_predominant,biomes_occurring) VALUES(?,?,?,?,?,?,?,?,?,?,1,?,?,?,?,?)");
       s.b(1,id);s.b(2,j["project_id"]);s.b(3,tid);
       bind_opt(s,4,j.contains("project_group_id")&&!j["project_group_id"].is_null()?std::optional<std::string>(j["project_group_id"]):std::nullopt);
       s.b(5,j["code"]);s.b(6,j["name"]);
       s.b(7,j.value("unit_type","monitoring_unit"));s.b(8,j.value("status","active"));
       s.b(9,ts);s.b(10,ts);
       bind_number_or_null(s,11,j,"latitude");bind_number_or_null(s,12,j,"longitude");
       bind_opt(s,13,j.contains("ibge_code")&&!j["ibge_code"].is_null()?std::optional<std::string>(j["ibge_code"]):std::nullopt);
       bind_opt(s,14,j.contains("biome_predominant")&&!j["biome_predominant"].is_null()?std::optional<std::string>(j["biome_predominant"]):std::nullopt);
       bind_opt(s,15,j.contains("biomes_occurring")&&!j["biomes_occurring"].is_null()?(j["biomes_occurring"].is_string()?std::optional<std::string>(j["biomes_occurring"]):std::optional<std::string>(j["biomes_occurring"].dump())):std::nullopt);
       s.row();
       audit(d,u->id,j["project_id"],"unit",id,"create",j);
       d.exec("COMMIT");
       respond(z,{{"data",{{"id",id},{"revision",1}}}},201);
     }catch(...){d.exec("ROLLBACK");throw;}
   }catch(const std::exception&e){error(z,422,"validation_error",e.what());}
 });
 app.Get(R"(/api/v1/units/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);if(!require(d,r,z))return;
   auto a=rows(d,"SELECT u.id,u.project_id,u.project_group_id,u.code,u.name,u.unit_type,u.status,u.revision,u.latitude,u.longitude,u.ibge_code,m.name municipality,c.name corede,rf.name functional_region,COALESCE(u.biome_predominant,b.name) biome,u.biome_predominant,u.biomes_occurring FROM units u JOIN territories m ON m.id=u.territory_id LEFT JOIN territories c ON c.id=m.parent_id AND c.kind='corede' LEFT JOIN territories rf ON rf.id=c.parent_id AND rf.kind='functional_region' LEFT JOIN territories b ON b.id=rf.parent_id AND b.kind='biome' WHERE u.id=? AND u.deleted_at IS NULL",[&](St&s){s.b(1,r.matches[1]);});
   if(a.empty())return error(z,404,"not_found","Unidade não encontrada");
   respond(z,{{"data",a[0]}});
 });
 app.Patch(R"(/api/v1/units/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){
   try{
     Db d(p.db);auto u=require(d,r,z,true);if(!u)return;
     auto j=json::parse(r.body);auto id=r.matches[1].str();int rev=j.value("revision",0);
     St old(d,"SELECT u.project_id,u.code,u.name,u.unit_type,u.status,u.revision,u.territory_id,m.name municipality,c.name corede,rf.name functional_region,u.biome_predominant,u.ibge_code,u.biomes_occurring FROM units u JOIN territories m ON m.id=u.territory_id LEFT JOIN territories c ON c.id=m.parent_id AND c.kind='corede' LEFT JOIN territories rf ON rf.id=c.parent_id AND rf.kind='functional_region' WHERE u.id=? AND u.deleted_at IS NULL");
     old.b(1,id);
     if(!old.row())return error(z,404,"not_found","Unidade não encontrada");
     if(rev!=old.num(5))return error(z,409,"revision_conflict","Revisão desatualizada");
     d.exec("BEGIN IMMEDIATE");
     try{
       if(!j.contains("municipality"))j["municipality"]=old.str(7);
       if(!j.contains("corede"))j["corede"]=old.str(8);
       if(!j.contains("functional_region"))j["functional_region"]=old.str(9);
       if(!j.contains("biome_predominant"))j["biome_predominant"]=old.str(10);
       if(!j.contains("ibge_code")&&!old.null(11))j["ibge_code"]=old.str(11);
       if(!j.contains("biomes_occurring")&&!old.null(12))j["biomes_occurring"]=old.str(12);
       auto tid=territory_chain(d,j,now());
       St s(d,"UPDATE units SET code=?,name=?,unit_type=?,status=?,territory_id=?,latitude=COALESCE(?,latitude),longitude=COALESCE(?,longitude),ibge_code=COALESCE(?,ibge_code),biome_predominant=COALESCE(?,biome_predominant),biomes_occurring=COALESCE(?,biomes_occurring),updated_at=?,revision=revision+1 WHERE id=? AND revision=?");
       s.b(1,j.value("code",old.str(1)));
       s.b(2,j.value("name",old.str(2)));
       s.b(3,j.value("unit_type",old.str(3)));
       s.b(4,j.value("status",old.str(4)));
       s.b(5,tid);
       bind_number_or_null(s,6,j,"latitude");
       bind_number_or_null(s,7,j,"longitude");
       bind_opt(s,8,j.contains("ibge_code")&&!j["ibge_code"].is_null()?std::optional<std::string>(j["ibge_code"]):std::nullopt);
       bind_opt(s,9,j.contains("biome_predominant")&&!j["biome_predominant"].is_null()?std::optional<std::string>(j["biome_predominant"]):std::nullopt);
       bind_opt(s,10,j.contains("biomes_occurring")&&!j["biomes_occurring"].is_null()?(j["biomes_occurring"].is_string()?std::optional<std::string>(j["biomes_occurring"]):std::optional<std::string>(j["biomes_occurring"].dump())):std::nullopt);
       s.b(11,now());
       s.b(12,id);
       s.bi(13,rev);
       s.row();
       audit(d,u->id,old.str(0),"unit",id,"update",j);
       d.exec("COMMIT");
       respond(z,{{"data",{{"id",id},{"revision",rev+1}}}});
     }catch(...){d.exec("ROLLBACK");throw;}
   }catch(const std::exception&e){error(z,422,"validation_error",e.what());}
 });
 app.Delete(R"(/api/v1/units/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);auto u=require(d,r,z,true);if(!u)return;
   auto id=r.matches[1].str();
   St old(d,"SELECT project_id FROM units WHERE id=? AND deleted_at IS NULL");
   old.b(1,id);
   if(!old.row())return error(z,404,"not_found","Unidade não encontrada");
   St s(d,"UPDATE units SET status='inactive',deleted_at=?,updated_at=?,revision=revision+1 WHERE id=?");
   s.b(1,now());s.b(2,now());s.b(3,id);s.row();
   audit(d,u->id,old.str(0),"unit",id,"delete",json::object());
   respond(z,{{"ok",true}});
 });
 app.Get("/api/v1/sources",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;respond(z,{{"data",rows(d,"SELECT id,project_id,title,kind,external_name,sha256,reference_period,provenance_status FROM source_documents ORDER BY created_at DESC")}});});
 app.Get("/api/v1/analytics/overview",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);if(!require(d,r,z))return;
   std::optional<std::string>g,u,c,rf,b;
   if(r.has_param("group_id")&&!r.get_param_value("group_id").empty())g=r.get_param_value("group_id");
   if(r.has_param("unit_id")&&!r.get_param_value("unit_id").empty())u=r.get_param_value("unit_id");
   if(r.has_param("corede")&&!r.get_param_value("corede").empty())c=r.get_param_value("corede");
   if(r.has_param("functional_region")&&!r.get_param_value("functional_region").empty())rf=r.get_param_value("functional_region");
   if(r.has_param("biome")&&!r.get_param_value("biome").empty())b=r.get_param_value("biome");
   respond(z,overview(p,g,u,r.has_param("project_id")?std::optional<std::string>(r.get_param_value("project_id")):std::nullopt,c,rf,b));
 });
 app.Get("/api/v1/analytics/units",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);if(!require(d,r,z))return;
   std::string q="SELECT unit_id,unit_name,municipality,ibge_code,corede,functional_region,biome_predominant,project_group_id,group_code,total,women,men,youth,quality_status,source_locator FROM v_attendance WHERE project_id=?";
   std::vector<std::string>params={r.get_param_value("project_id")};
   if(r.has_param("group_id")&&!r.get_param_value("group_id").empty()){q+=" AND project_group_id=?";params.push_back(r.get_param_value("group_id"));}
   if(r.has_param("unit_id")&&!r.get_param_value("unit_id").empty()){q+=" AND unit_id=?";params.push_back(r.get_param_value("unit_id"));}
   if(r.has_param("corede")&&!r.get_param_value("corede").empty()){q+=" AND corede=?";params.push_back(r.get_param_value("corede"));}
   if(r.has_param("functional_region")&&!r.get_param_value("functional_region").empty()){q+=" AND functional_region=?";params.push_back(r.get_param_value("functional_region"));}
   if(r.has_param("biome")&&!r.get_param_value("biome").empty()){q+=" AND (biome_predominant=? OR biomes_occurring LIKE ?)";params.push_back(r.get_param_value("biome"));params.push_back("%"+r.get_param_value("biome")+"%");}
   q+=" ORDER BY unit_name";
   respond(z,{{"data",rows(d,q,[&](St&s){for(size_t i=0;i<params.size();++i)s.b(i+1,params[i]);})}});
 });
 app.Get("/api/v1/analytics/dimensions",[p](const httplib::Request&r,httplib::Response&z){
   Db d(p.db);if(!require(d,r,z))return;
   auto pid=r.get_param_value("project_id");
   auto coredes=rows(d,"SELECT DISTINCT c.name FROM units u JOIN territories m ON m.id=u.territory_id JOIN territories c ON c.id=m.parent_id AND c.kind='corede' WHERE u.project_id=? AND u.deleted_at IS NULL ORDER BY c.name",[&](St&s){s.b(1,pid);});
   auto rfs=rows(d,"SELECT DISTINCT rf.name FROM units u JOIN territories m ON m.id=u.territory_id JOIN territories c ON c.id=m.parent_id AND c.kind='corede' JOIN territories rf ON rf.id=c.parent_id AND rf.kind='functional_region' WHERE u.project_id=? AND u.deleted_at IS NULL ORDER BY rf.name",[&](St&s){s.b(1,pid);});
   auto biomes=rows(d,"SELECT DISTINCT biome_predominant name FROM units WHERE project_id=? AND deleted_at IS NULL AND biome_predominant IS NOT NULL ORDER BY biome_predominant",[&](St&s){s.b(1,pid);});
   respond(z,{{"data",{{"coredes",coredes},{"functional_regions",rfs},{"biomes",biomes}}}});
 });
 app.Get("/api/v1/analytics/groups",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;respond(z,{{"data",rows(d,"SELECT project_group_id id,group_code code,sum(total) total,sum(women) women,sum(men) men,sum(youth) youth FROM v_attendance WHERE project_id=? GROUP BY project_group_id,group_code ORDER BY group_code",[&](St&s){s.b(1,r.get_param_value("project_id"));})}});});
 auto list=[p](const char*table,const char*cols){return [p,table=std::string(table),cols=std::string(cols)](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;std::string q="SELECT "+cols+" FROM "+table+" WHERE project_id=? AND deleted_at IS NULL ORDER BY created_at DESC LIMIT 200";respond(z,{{"data",rows(d,q,[&](St&s){s.b(1,r.get_param_value("project_id"));})}});};};
 app.Get("/api/v1/activities",list("activities","id,project_id,unit_id,kind,title,description,occurred_at,status,created_at,updated_at,revision"));
 app.Get("/api/v1/action-items",list("action_items","id,project_id,activity_id,unit_id,title,details,status,priority,due_at,created_at,updated_at,revision"));
 app.Get("/api/v1/observations",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;respond(z,{{"data",rows(d,"SELECT o.id,o.activity_id,o.unit_id,o.topic,o.statement,o.observation_kind,o.epistemic_status,o.observed_at,o.source_id,o.source_locator,o.validation_status,o.created_at,o.updated_at,o.revision FROM observations o JOIN activities a ON a.id=o.activity_id WHERE a.project_id=? AND o.deleted_at IS NULL ORDER BY o.created_at DESC",[&](St&s){s.b(1,r.get_param_value("project_id"));})}});});
 app.Post("/api/v1/activities",[p](const httplib::Request&r,httplib::Response&z){try{Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto j=json::parse(r.body);if(j.value("project_id","").empty()||j.value("title","").empty())return error(z,422,"validation_error","Projeto e título são obrigatórios");auto id="activity-"+random_hex(12),ts=now();d.exec("BEGIN IMMEDIATE");try{St s(d,"INSERT INTO activities VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)");s.b(1,id);s.b(2,j["project_id"]);bind_opt(s,3,j.contains("unit_id")&&!j["unit_id"].is_null()?std::optional<std::string>(j["unit_id"]):std::nullopt);s.b(4,j.value("kind","field_activity"));s.b(5,j["title"]);s.b(6,j.value("description",""));bind_opt(s,7,j.contains("occurred_at")&&!j["occurred_at"].is_null()?std::optional<std::string>(j["occurred_at"]):std::nullopt);s.b(8,j.value("status","planned"));s.b(9,u->id);s.b(10,ts);s.b(11,ts);s.bi(12,1);s.bn(13);s.row();audit(d,u->id,j["project_id"],"activity",id,"create",j);d.exec("COMMIT");respond(z,{{"data",{{"id",id},{"revision",1}}}},201);}catch(...){d.exec("ROLLBACK");throw;}}catch(const std::exception&e){error(z,422,"validation_error",e.what());}});
 app.Post("/api/v1/observations",[p](const httplib::Request&r,httplib::Response&z){try{Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto j=json::parse(r.body);if(j.value("activity_id","").empty()||j.value("statement","").empty())return error(z,422,"validation_error","Atividade e declaração são obrigatórias");St project(d,"SELECT project_id FROM activities WHERE id=?");project.b(1,j["activity_id"]);if(!project.row())return error(z,404,"not_found","Atividade não encontrada");auto pid=project.str(0),id="observation-"+random_hex(12),ts=now();d.exec("BEGIN IMMEDIATE");try{St s(d,"INSERT INTO observations VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");s.b(1,id);s.b(2,j["activity_id"]);bind_opt(s,3,j.contains("unit_id")&&!j["unit_id"].is_null()?std::optional<std::string>(j["unit_id"]):std::nullopt);s.b(4,j.value("topic","geral"));s.b(5,j["statement"]);s.b(6,j.value("observation_kind","reported"));s.b(7,j.value("epistemic_status","observed"));bind_opt(s,8,j.contains("observed_at")&&!j["observed_at"].is_null()?std::optional<std::string>(j["observed_at"]):std::nullopt);bind_opt(s,9,j.contains("source_id")&&!j["source_id"].is_null()?std::optional<std::string>(j["source_id"]):std::nullopt);bind_opt(s,10,j.contains("source_locator")&&!j["source_locator"].is_null()?std::optional<std::string>(j["source_locator"]):std::nullopt);s.b(11,u->id);s.b(12,j.value("validation_status","draft"));s.b(13,ts);s.b(14,ts);s.bi(15,1);s.bn(16);s.row();audit(d,u->id,pid,"observation",id,"create",j);d.exec("COMMIT");respond(z,{{"data",{{"id",id},{"revision",1}}}},201);}catch(...){d.exec("ROLLBACK");throw;}}catch(const std::exception&e){error(z,422,"validation_error",e.what());}});
 app.Post("/api/v1/action-items",[p](const httplib::Request&r,httplib::Response&z){try{Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto j=json::parse(r.body);if(j.value("project_id","").empty()||j.value("title","").empty())return error(z,422,"validation_error","Projeto e título são obrigatórios");auto id="action-"+random_hex(12),ts=now();d.exec("BEGIN IMMEDIATE");try{St s(d,"INSERT INTO action_items VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)");s.b(1,id);s.b(2,j["project_id"]);bind_opt(s,3,j.contains("activity_id")&&!j["activity_id"].is_null()?std::optional<std::string>(j["activity_id"]):std::nullopt);bind_opt(s,4,j.contains("unit_id")&&!j["unit_id"].is_null()?std::optional<std::string>(j["unit_id"]):std::nullopt);s.b(5,j["title"]);s.b(6,j.value("details",""));s.b(7,j.value("status","open"));s.b(8,j.value("priority","normal"));bind_opt(s,9,j.contains("due_at")&&!j["due_at"].is_null()?std::optional<std::string>(j["due_at"]):std::nullopt);s.bn(10);s.b(11,u->id);s.b(12,ts);s.b(13,ts);s.bi(14,1);s.bn(15);s.row();audit(d,u->id,j["project_id"],"action_item",id,"create",j);d.exec("COMMIT");respond(z,{{"data",{{"id",id},{"revision",1}}}},201);}catch(...){d.exec("ROLLBACK");throw;}}catch(const std::exception&e){error(z,422,"validation_error",e.what());}});
 app.Get(R"(/api/v1/activities/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;auto a=rows(d,"SELECT id,project_id,unit_id,kind,title,description,occurred_at,status,revision FROM activities WHERE id=? AND deleted_at IS NULL",[&](St&s){s.b(1,r.matches[1]);});if(a.empty())return error(z,404,"not_found","Atividade não encontrada");respond(z,{{"data",a[0]}});});
 app.Patch(R"(/api/v1/activities/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){try{Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto j=json::parse(r.body);auto id=r.matches[1].str();int rev=j.value("revision",0);St old(d,"SELECT project_id,kind,title,description,status,revision FROM activities WHERE id=? AND deleted_at IS NULL");old.b(1,id);if(!old.row())return error(z,404,"not_found","Atividade não encontrada");if(rev!=old.num(5))return error(z,409,"revision_conflict","Revisão desatualizada");St s(d,"UPDATE activities SET kind=?,title=?,description=?,status=?,updated_at=?,revision=revision+1 WHERE id=? AND revision=?");s.b(1,j.value("kind",old.str(1)));s.b(2,j.value("title",old.str(2)));s.b(3,j.value("description",old.str(3)));s.b(4,j.value("status",old.str(4)));s.b(5,now());s.b(6,id);s.bi(7,rev);s.row();audit(d,u->id,old.str(0),"activity",id,"update",j);respond(z,{{"data",{{"id",id},{"revision",rev+1}}}});}catch(const std::exception&e){error(z,422,"validation_error",e.what());}});
 app.Delete(R"(/api/v1/activities/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto id=r.matches[1].str();St old(d,"SELECT project_id FROM activities WHERE id=? AND deleted_at IS NULL");old.b(1,id);if(!old.row())return error(z,404,"not_found","Atividade não encontrada");St s(d,"UPDATE activities SET deleted_at=?,updated_at=?,revision=revision+1 WHERE id=?");s.b(1,now());s.b(2,now());s.b(3,id);s.row();audit(d,u->id,old.str(0),"activity",id,"delete",json::object());respond(z,{{"ok",true}});});
 app.Get(R"(/api/v1/observations/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;auto a=rows(d,"SELECT id,activity_id,unit_id,topic,statement,observation_kind,epistemic_status,observed_at,source_id,source_locator,validation_status,revision FROM observations WHERE id=? AND deleted_at IS NULL",[&](St&s){s.b(1,r.matches[1]);});if(a.empty())return error(z,404,"not_found","Observação não encontrada");respond(z,{{"data",a[0]}});});
 app.Patch(R"(/api/v1/observations/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){try{Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto j=json::parse(r.body);auto id=r.matches[1].str();int rev=j.value("revision",0);St old(d,"SELECT a.project_id,o.topic,o.statement,o.observation_kind,o.epistemic_status,o.validation_status,o.revision FROM observations o JOIN activities a ON a.id=o.activity_id WHERE o.id=? AND o.deleted_at IS NULL");old.b(1,id);if(!old.row())return error(z,404,"not_found","Observação não encontrada");if(rev!=old.num(6))return error(z,409,"revision_conflict","Revisão desatualizada");auto validation=j.value("validation_status",old.str(5));if(old.str(5)=="validated"&&!j.contains("validation_status"))validation="review_required";St s(d,"UPDATE observations SET topic=?,statement=?,observation_kind=?,epistemic_status=?,validation_status=?,updated_at=?,revision=revision+1 WHERE id=? AND revision=?");s.b(1,j.value("topic",old.str(1)));s.b(2,j.value("statement",old.str(2)));s.b(3,j.value("observation_kind",old.str(3)));s.b(4,j.value("epistemic_status",old.str(4)));s.b(5,validation);s.b(6,now());s.b(7,id);s.bi(8,rev);s.row();audit(d,u->id,old.str(0),"observation",id,"update",j);respond(z,{{"data",{{"id",id},{"revision",rev+1},{"validation_status",validation}}}});}catch(const std::exception&e){error(z,422,"validation_error",e.what());}});
 app.Delete(R"(/api/v1/observations/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto id=r.matches[1].str();St old(d,"SELECT a.project_id FROM observations o JOIN activities a ON a.id=o.activity_id WHERE o.id=? AND o.deleted_at IS NULL");old.b(1,id);if(!old.row())return error(z,404,"not_found","Observação não encontrada");St s(d,"UPDATE observations SET deleted_at=?,updated_at=?,revision=revision+1 WHERE id=?");s.b(1,now());s.b(2,now());s.b(3,id);s.row();audit(d,u->id,old.str(0),"observation",id,"delete",json::object());respond(z,{{"ok",true}});});
 app.Get(R"(/api/v1/action-items/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;auto a=rows(d,"SELECT id,project_id,activity_id,unit_id,title,details,status,priority,due_at,revision FROM action_items WHERE id=? AND deleted_at IS NULL",[&](St&s){s.b(1,r.matches[1]);});if(a.empty())return error(z,404,"not_found","Encaminhamento não encontrado");respond(z,{{"data",a[0]}});});
 app.Patch(R"(/api/v1/action-items/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){try{Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto j=json::parse(r.body);std::string id=r.matches[1].str();int rev=j.value("revision",0);St old(d,"SELECT project_id,title,details,status,priority,revision FROM action_items WHERE id=? AND deleted_at IS NULL");old.b(1,id);if(!old.row())return error(z,404,"not_found","Encaminhamento não encontrado");if(old.num(5)!=rev)return error(z,409,"revision_conflict","Revisão desatualizada");std::string status=j.value("status",old.str(3));static const std::vector<std::string>valid={"open","in_progress","completed","blocked","cancelled"};if(std::find(valid.begin(),valid.end(),status)==valid.end())return error(z,422,"validation_error","Estado inválido");St s(d,"UPDATE action_items SET title=?,details=?,status=?,priority=?,updated_at=?,revision=revision+1 WHERE id=? AND revision=?");s.b(1,j.value("title",old.str(1)));s.b(2,j.value("details",old.str(2)));s.b(3,status);s.b(4,j.value("priority",old.str(4)));s.b(5,now());s.b(6,id);s.bi(7,rev);s.row();audit(d,u->id,old.str(0),"action_item",id,"update",j);respond(z,{{"data",{{"id",id},{"status",status},{"revision",rev+1}}}});}catch(const std::exception&e){error(z,422,"validation_error",e.what());}});
 app.Delete(R"(/api/v1/action-items/([A-Za-z0-9-]+))",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto id=r.matches[1].str();St old(d,"SELECT project_id FROM action_items WHERE id=? AND deleted_at IS NULL");old.b(1,id);if(!old.row())return error(z,404,"not_found","Encaminhamento não encontrado");St s(d,"UPDATE action_items SET deleted_at=?,updated_at=?,revision=revision+1 WHERE id=?");s.b(1,now());s.b(2,now());s.b(3,id);s.row();audit(d,u->id,old.str(0),"action_item",id,"delete",json::object());respond(z,{{"ok",true}});});
  auto build_export_query = [](const httplib::Request& r, std::string& q, std::vector<std::string>& params) {
    q = "SELECT va.group_code, va.unit_name, va.municipality, va.ibge_code, va.corede, va.functional_region, va.biome_predominant, va.total, va.women, va.men, va.youth, va.quality_status, va.source_locator FROM v_attendance va WHERE va.project_id=?";
    params = {r.get_param_value("project_id")};
    if (r.has_param("group_id") && !r.get_param_value("group_id").empty()) { q += " AND va.project_group_id=?"; params.push_back(r.get_param_value("group_id")); }
    if (r.has_param("unit_id") && !r.get_param_value("unit_id").empty()) { q += " AND va.unit_id=?"; params.push_back(r.get_param_value("unit_id")); }
    if (r.has_param("corede") && !r.get_param_value("corede").empty()) { q += " AND va.corede=?"; params.push_back(r.get_param_value("corede")); }
    if (r.has_param("functional_region") && !r.get_param_value("functional_region").empty()) { q += " AND va.functional_region=?"; params.push_back(r.get_param_value("functional_region")); }
    if (r.has_param("biome") && !r.get_param_value("biome").empty()) { q += " AND (va.biome_predominant=? OR va.biomes_occurring LIKE ?)"; params.push_back(r.get_param_value("biome")); params.push_back("%" + r.get_param_value("biome") + "%"); }
    q += " ORDER BY va.group_code, va.unit_name";
  };
  app.Get("/api/v1/exports/attendance.json",[p,build_export_query](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;std::string q;std::vector<std::string>params;build_export_query(r,q,params);auto data=rows(d,q,[&](St&s){for(size_t i=0;i<params.size();++i)s.b(i+1,params[i]);});respond(z,{{"metadata",{{"generated_at",now()},{"rule_version","attendance-v1"},{"provenance","document_aggregate"}}},{"data",data}});});
  app.Get("/api/v1/exports/attendance.csv",[p,build_export_query](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;std::string q;std::vector<std::string>params;build_export_query(r,q,params);auto data=rows(d,q,[&](St&s){for(size_t i=0;i<params.size();++i)s.b(i+1,params[i]);});auto cell=[](std::string v){if(!v.empty()&&std::string("=+-@").find(v[0])!=std::string::npos)v="'"+v;size_t pos=0;while((pos=v.find('"',pos))!=std::string::npos){v.insert(pos,"\"");pos+=2;}return "\""+v+"\"";};std::string csv="\xEF\xBB\xBFgrupo,uac,municipio,ibge,corede,regiao_funcional,bioma,total,mulheres,homens,jovens,qualidade,fonte\r\n";for(auto&x:data)csv+=cell(x.value("group_code",""))+","+cell(x.value("unit_name",""))+","+cell(x.value("municipality",""))+","+cell(x.value("ibge_code",""))+","+cell(x.value("corede",""))+","+cell(x.value("functional_region",""))+","+cell(x.value("biome_predominant",""))+","+std::to_string(x.value("total",0))+","+std::to_string(x.value("women",0))+","+std::to_string(x.value("men",0))+","+std::to_string(x.value("youth",0))+","+cell(x.value("quality_status",""))+","+cell(x.value("source_locator",""))+"\r\n";z.set_header("Content-Disposition","attachment; filename=trama-participacoes.csv");z.set_content(csv,"text/csv; charset=utf-8");headers(z);});
  auto handle_report=[p](const httplib::Request&r,httplib::Response&z){
    try{
      Db d(p.db);
      auto u=require(d,r,z,false);
      if(!u)return;
      std::string pid,gid,uid,corede,rf,biome,map_layer="municipios",map_metric="total";
      bool inc_map=true;
      json params=json::object();
      if(!r.body.empty()){try{params=json::parse(r.body);}catch(...){}}
      auto get_p=[&](const std::string&k)->std::string{if(r.has_param(k)&&!r.get_param_value(k).empty())return r.get_param_value(k);if(params.contains(k)&&params[k].is_string())return params[k].get<std::string>();return "";};
      pid=get_p("project_id");
      gid=get_p("group_id");if(gid.empty())gid=get_p("group");
      uid=get_p("unit_id");if(uid.empty())uid=get_p("unit");
      corede=get_p("corede");
      rf=get_p("functional_region");if(rf.empty())rf=get_p("rf");
      biome=get_p("biome");
      if(r.has_param("map_layer")&&!r.get_param_value("map_layer").empty())map_layer=r.get_param_value("map_layer");else if(params.contains("map_layer")&&params["map_layer"].is_string())map_layer=params["map_layer"];
      if(r.has_param("map_metric")&&!r.get_param_value("map_metric").empty())map_metric=r.get_param_value("map_metric");else if(params.contains("map_metric")&&params["map_metric"].is_string())map_metric=params["map_metric"];
      if(r.has_param("include_map")){std::string val=r.get_param_value("include_map");inc_map=(val!="0"&&val!="false");}else if(params.contains("include_map")){if(params["include_map"].is_boolean())inc_map=params["include_map"].get<bool>();else if(params["include_map"].is_string())inc_map=(params["include_map"]!="0"&&params["include_map"]!="false");}
      if(pid.empty()){pid=scalar(d,"SELECT id FROM projects WHERE status != 'archived' ORDER BY created_at LIMIT 1");if(pid.empty())return error(z,422,"no_project","Nenhum projeto cadastrado para gerar relatório");}
      params["project_id"]=pid;if(!gid.empty())params["group_id"]=gid;if(!uid.empty())params["unit_id"]=uid;if(!corede.empty())params["corede"]=corede;if(!rf.empty())params["functional_region"]=rf;if(!biome.empty())params["biome"]=biome;params["include_map"]=inc_map;params["map_layer"]=map_layer;params["map_metric"]=map_metric;

      St proj_st(d,"SELECT code,name,description,status,period_start,period_end FROM projects WHERE id=?");
      proj_st.b(1,pid);
      if(!proj_st.row())return error(z,404,"not_found","Projeto não encontrado");
      std::string pcode=proj_st.str(0),pname=proj_st.str(1),pdesc=proj_st.str(2),pstatus=proj_st.str(3),pstart=proj_st.null(4)?"":proj_st.str(4),pend=proj_st.null(5)?"":proj_st.str(5);

      std::string group_label_sel;if(!gid.empty()){St gs(d,"SELECT label,code FROM project_groups WHERE id=?");gs.b(1,gid);if(gs.row())group_label_sel=gs.str(0)+" ("+gs.str(1)+")";}
      std::string unit_name_sel;if(!uid.empty()){St us(d,"SELECT name FROM units WHERE id=?");us.b(1,uid);if(us.row())unit_name_sel=us.str(0);}

      auto ov=overview(p,gid.empty()?std::nullopt:std::make_optional(gid),uid.empty()?std::nullopt:std::make_optional(uid),pid,corede.empty()?std::nullopt:std::make_optional(corede),rf.empty()?std::nullopt:std::make_optional(rf),biome.empty()?std::nullopt:std::make_optional(biome));

      std::string data_q="SELECT va.unit_id,va.unit_name,va.project_group_id,va.group_code,COALESCE(va.group_label,va.group_code) group_label,va.municipality,va.ibge_code,va.corede,va.functional_region,va.biome_predominant,va.biomes_occurring,va.total,va.women,va.men,va.youth,va.quality_status,va.source_locator,u.latitude,u.longitude FROM v_attendance va JOIN units u ON u.id=va.unit_id WHERE va.project_id=?";
      std::vector<std::string>qparams={pid};
      if(!gid.empty()){data_q+=" AND va.project_group_id=?";qparams.push_back(gid);}
      if(!uid.empty()){data_q+=" AND va.unit_id=?";qparams.push_back(uid);}
      if(!corede.empty()){data_q+=" AND va.corede=?";qparams.push_back(corede);}
      if(!rf.empty()){data_q+=" AND va.functional_region=?";qparams.push_back(rf);}
      if(!biome.empty()){data_q+=" AND (va.biome_predominant=? OR va.biomes_occurring LIKE ?)";qparams.push_back(biome);qparams.push_back("%"+biome+"%");}
      data_q+=" ORDER BY va.group_code, va.unit_name";
      auto data=rows(d,data_q,[&](St&s){for(size_t i=0;i<qparams.size();++i)s.b(i+1,qparams[i]);});

      std::string grp_q="SELECT va.group_code,COALESCE(va.group_label,va.group_code) group_label,COUNT(DISTINCT va.unit_id) units_count,COALESCE(SUM(va.total),0) total,COALESCE(SUM(va.women),0) women,COALESCE(SUM(va.men),0) men,COALESCE(SUM(va.youth),0) youth FROM v_attendance va JOIN units u ON u.id=va.unit_id WHERE va.project_id=?";
      if(!gid.empty())grp_q+=" AND va.project_group_id='"+gid+"'";
      if(!uid.empty())grp_q+=" AND va.unit_id='"+uid+"'";
      if(!corede.empty())grp_q+=" AND va.corede='"+corede+"'";
      if(!rf.empty())grp_q+=" AND va.functional_region='"+rf+"'";
      if(!biome.empty())grp_q+=" AND (va.biome_predominant='"+biome+"' OR va.biomes_occurring LIKE '%"+biome+"%')";
      grp_q+=" GROUP BY va.group_code, va.group_label ORDER BY va.group_code";
      auto grp_data=rows(d,grp_q,[&](St&s){s.b(1,pid);});

      auto action_items=rows(d,"SELECT ai.id,ai.title,ai.details,ai.status,ai.priority,ai.due_at,u.name unit_name FROM action_items ai LEFT JOIN units u ON u.id=ai.unit_id WHERE ai.project_id=? AND ai.deleted_at IS NULL ORDER BY CASE ai.status WHEN 'open' THEN 1 WHEN 'in_progress' THEN 2 WHEN 'blocked' THEN 3 ELSE 4 END, ai.created_at DESC LIMIT 30",[&](St&s){s.b(1,pid);});
      auto observations=rows(d,"SELECT o.topic,o.statement,o.observation_kind,o.epistemic_status,o.validation_status,u.name unit_name FROM observations o JOIN activities a ON a.id=o.activity_id LEFT JOIN units u ON u.id=o.unit_id WHERE a.project_id=? AND o.deleted_at IS NULL ORDER BY o.created_at DESC LIMIT 30",[&](St&s){s.b(1,pid);});

      int total_attendances=ov["kpis"].value("reported_attendances",0);
      int total_women=ov["kpis"].value("reported_women",0);
      int total_men=ov["kpis"].value("reported_men",0);
      int total_youth=ov["kpis"].value("reported_youth",0);
      int total_uacs=ov["kpis"].value("uacs_documented",0);
      int pending_actions=ov["kpis"].value("pending_action_items",0);

      auto format_pct=[](double val)->std::string{char buf[32];snprintf(buf,sizeof(buf),"%.1f%%",val);return std::string(buf);};
      std::string pct_women=ov["kpis"]["female_share"].is_null()?"—":format_pct(ov["kpis"]["female_share"].get<double>());
      std::string pct_youth=ov["kpis"]["youth_share"].is_null()?"—":format_pct(ov["kpis"]["youth_share"].get<double>());
      std::string pct_men=(total_attendances>0)?format_pct(total_men*100.0/total_attendances):"—";

      std::string id="report-"+random_hex(12);
      std::string html="<!doctype html><html lang=\"pt-BR\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>Relatório Técnico TRAMA — "+esc(pname)+"</title>";
      html+="<link rel=\"stylesheet\" href=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.css\" />";
      html+="<style>";
      html+="body{font-family:system-ui,-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,Helvetica,Arial,sans-serif;max-width:1150px;margin:1.5rem auto;padding:0 1.5rem;color:#1e293b;line-height:1.5;background:#f8fafc}";
      html+=".report-container{background:#fff;border:1px solid #cbd5e1;border-radius:12px;padding:2.5rem;box-shadow:0 4px 6px -1px rgba(0,0,0,0.05)}";
      html+=".header-top{display:flex;justify-content:space-between;align-items:flex-start;border-bottom:3px solid #0e503e;padding-bottom:1rem;margin-bottom:1.25rem}";
      html+=".logo-badge{font-size:0.85rem;font-weight:700;letter-spacing:1px;color:#0e503e;background:#d6e8df;padding:0.25rem 0.6rem;border-radius:4px;display:inline-block;margin-bottom:0.5rem}";
      html+="h1{color:#0f172a;margin:0 0 0.25rem 0;font-size:1.9rem;letter-spacing:-0.5px}";
      html+=".meta-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(220px,1fr));gap:0.75rem;background:#f1f5f9;border:1px solid #e2e8f0;border-radius:8px;padding:1rem 1.25rem;margin-bottom:1.5rem;font-size:0.88rem;color:#475569}";
      html+=".filter-scope{margin-bottom:1.5rem;background:#f0fdf4;border:1px solid #bbf7d0;border-radius:8px;padding:0.85rem 1.25rem}";
      html+=".filter-scope strong{color:#166534}";
      html+=".filter-chips{display:flex;flex-wrap:wrap;gap:0.5rem;margin-top:0.4rem}";
      html+=".chip{background:#fff;border:1px solid #86efac;color:#14532d;font-size:0.8rem;padding:0.2rem 0.6rem;border-radius:9999px;font-weight:600}";
      html+=".kpi-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(145px,1fr));gap:0.85rem;margin-bottom:2rem}";
      html+=".kpi-card{background:#f8fafc;border:1px solid #cbd5e1;border-radius:8px;padding:1rem;text-align:center;border-top:4px solid #0e503e}";
      html+=".kpi-val{font-size:1.75rem;font-weight:800;color:#0f172a;line-height:1.2}";
      html+=".kpi-sub{font-size:0.82rem;font-weight:600;color:#0e503e;margin-top:0.15rem}";
      html+=".kpi-lbl{font-size:0.82rem;color:#64748b;margin-top:0.3rem;text-transform:uppercase;letter-spacing:0.5px;font-weight:600}";
      html+="h2{color:#0f172a;font-size:1.25rem;margin:2rem 0 0.75rem 0;padding-bottom:0.4rem;border-bottom:1px solid #e2e8f0;display:flex;align-items:center;gap:0.5rem}";
      html+="table{border-collapse:collapse;width:100%;margin-top:0.75rem;font-size:0.88rem;background:#fff}";
      html+="th,td{padding:0.65rem 0.75rem;border:1px solid #cbd5e1;text-align:left}";
      html+="th{background:#f1f5f9;color:#334155;font-weight:700;font-size:0.82rem;text-transform:uppercase;letter-spacing:0.5px}";
      html+="tr:nth-child(even){background:#f8fafc}";
      html+="tr:hover{background:#f1f5f9}";
      html+="td.num,th.num{text-align:right}";
      html+="tfoot tr{background:#e2e8f0;font-weight:700}";
      html+=".tag{display:inline-block;padding:0.15rem 0.45rem;border-radius:4px;font-size:0.75rem;font-weight:600;background:#e2e8f0;color:#334155}";
      html+=".tag-green{background:#dcfce7;color:#166534}";
      html+=".tag-blue{background:#e0f2fe;color:#0369a1}";
      html+=".tag-amber{background:#fef3c7;color:#92400e}";
      html+=".map-box{background:#f8fafc;border:1px solid #cbd5e1;border-radius:8px;padding:1rem;margin-top:1rem}";
      html+="#report-map{height:400px;width:100%;border-radius:6px;border:1px solid #cbd5e1;background:#e8eee9}";
      html+=".provenance-box{background:#f8fafc;border-left:4px solid #0284c7;border-radius:0 8px 8px 0;padding:1rem 1.25rem;margin-top:2rem;font-size:0.85rem;color:#475569}";
      html+=".actions{display:flex;gap:0.75rem;margin-bottom:1.5rem}";
      html+="button.btn{background:#0e503e;color:#fff;border:none;padding:0.6rem 1.2rem;border-radius:6px;font-weight:600;cursor:pointer;font-size:0.9rem;display:inline-flex;align-items:center;gap:0.4rem}";
      html+="button.btn:hover{background:#173f35}";
      html+="@media print{.actions,.no-print{display:none !important}body{background:#fff;margin:0;padding:0;max-width:100%}.report-container{border:none;box-shadow:none;padding:0}#report-map{height:300px;page-break-inside:avoid}table{page-break-inside:auto}tr{page-break-inside:avoid;page-break-after:auto}h2{page-break-after:avoid}}";
      html+="</style>";
      html+="<script src=\"https://unpkg.com/leaflet@1.9.4/dist/leaflet.js\"></script>";
      html+="</head><body>";
      html+="<div class=\"actions no-print\">";
      html+="<button class=\"btn\" onclick=\"window.print()\">🖨️ Imprimir / Salvar como PDF</button>";
      html+="</div>";
      html+="<div class=\"report-container\">";
      html+="<div class=\"header-top\">";
      html+="<div>";
      html+="<div class=\"logo-badge\">SISTEMA TRAMA · RELATÓRIO TÉCNICO TERRITORIAL</div>";
      html+="<h1>"+esc(pname)+"</h1>";
      if(!pdesc.empty())html+="<p style=\"color:#475569;margin:0.25rem 0 0 0;font-size:0.95rem;\">"+esc(pdesc)+"</p>";
      html+="</div>";
      html+="<div style=\"text-align:right;font-size:0.85rem;color:#64748b;\">";
      html+="<div><strong>Código:</strong> "+esc(pcode)+"</div>";
      html+="<div><strong>Status:</strong> <span class=\"tag tag-green\">"+esc(pstatus)+"</span></div>";
      html+="</div>";
      html+="</div>";

      html+="<div class=\"meta-grid\">";
      html+="<div><strong>Data de Emissão:</strong> "+now()+"</div>";
      html+="<div><strong>Gerado por:</strong> "+esc(u->login)+" ("+esc(u->role)+")</div>";
      html+="<div><strong>Regra de Agregação:</strong> attendance-v1</div>";
      html+="<div><strong>Referência Territorial:</strong> TRAMA-RS / IBGE</div>";
      html+="</div>";

      html+="<div class=\"filter-scope\">";
      html+="<strong>Recorte e Parâmetros Analíticos Aplicados:</strong>";
      html+="<div class=\"filter-chips\">";
      if(!gid.empty()&&!group_label_sel.empty())html+="<span class=\"chip\">Grupo: "+esc(group_label_sel)+"</span>";
      if(!uid.empty()&&!unit_name_sel.empty())html+="<span class=\"chip\">Unidade: "+esc(unit_name_sel)+"</span>";
      if(!corede.empty())html+="<span class=\"chip\">COREDE: "+esc(corede)+"</span>";
      if(!rf.empty())html+="<span class=\"chip\">Região Funcional: "+esc(rf)+"</span>";
      if(!biome.empty())html+="<span class=\"chip\">Bioma: "+esc(biome)+"</span>";
      if(gid.empty()&&uid.empty()&&corede.empty()&&rf.empty()&&biome.empty())html+="<span class=\"chip\">Âmbito: Projeto Completo (Todos os Grupos e Territórios)</span>";
      html+="</div>";
      html+="</div>";

      html+="<h2>📊 Sumário Executivo de Indicadores</h2>";
      html+="<div class=\"kpi-grid\">";
      html+="<div class=\"kpi-card\"><div class=\"kpi-val\">"+std::to_string(total_attendances)+"</div><div class=\"kpi-lbl\">Participações Totais</div></div>";
      html+="<div class=\"kpi-card\"><div class=\"kpi-val\">"+std::to_string(total_women)+"</div><div class=\"kpi-sub\">"+pct_women+"</div><div class=\"kpi-lbl\">Mulheres</div></div>";
      html+="<div class=\"kpi-card\"><div class=\"kpi-val\">"+std::to_string(total_men)+"</div><div class=\"kpi-sub\">"+pct_men+"</div><div class=\"kpi-lbl\">Homens</div></div>";
      html+="<div class=\"kpi-card\"><div class=\"kpi-val\">"+std::to_string(total_youth)+"</div><div class=\"kpi-sub\">"+pct_youth+"</div><div class=\"kpi-lbl\">Jovens</div></div>";
      html+="<div class=\"kpi-card\"><div class=\"kpi-val\">"+std::to_string(total_uacs)+"</div><div class=\"kpi-lbl\">UACs Documentadas</div></div>";
      html+="<div class=\"kpi-card\"><div class=\"kpi-val\">"+std::to_string(pending_actions)+"</div><div class=\"kpi-lbl\">Ações Pendentes</div></div>";
      html+="</div>";

      if(inc_map){
        html+="<h2>🗺️ Distribuição Cartográfica e Espacial</h2>";
        html+="<div class=\"map-box\">";
        html+="<div id=\"report-map\"></div>";
        html+="<div style=\"display:flex;justify-content:space-between;margin-top:0.5rem;font-size:0.8rem;color:#64748b;\">";
        html+="<span>Camada temática: <strong>"+esc(map_layer)+"</strong> · Indicador: <strong>"+esc(map_metric)+"</strong></span>";
        html+="<span>Unidades com georreferenciamento demarcadas no mapa</span>";
        html+="</div>";
        html+="</div>";
      }

      html+="<h2>📑 Agrupamento Consolidado por Grupo</h2>";
      html+="<table><thead><tr><th>Grupo</th><th>Rótulo / Descrição</th><th class=\"num\">UACs</th><th class=\"num\">Total</th><th class=\"num\">Mulheres</th><th class=\"num\">% Fem.</th><th class=\"num\">Homens</th><th class=\"num\">Jovens</th><th class=\"num\">% Jovem</th></tr></thead><tbody>";
      if(grp_data.empty()){
        html+="<tr><td colspan=\"9\" style=\"text-align:center;color:#64748b;padding:1rem\">Nenhum grupo encontrado no recorte.</td></tr>";
      }else{
        for(auto&g:grp_data){
          int gt=g.value("total",0),gw=g.value("women",0),gm=g.value("men",0),gy=g.value("youth",0),gu=g.value("units_count",0);
          std::string g_pw=gt>0?format_pct(gw*100.0/gt):"—",g_py=gt>0?format_pct(gy*100.0/gt):"—";
          html+="<tr><td><strong>"+esc(g.value("group_code",""))+"</strong></td><td>"+esc(g.value("group_label",""))+"</td><td class=\"num\">"+std::to_string(gu)+"</td><td class=\"num\"><strong>"+std::to_string(gt)+"</strong></td><td class=\"num\">"+std::to_string(gw)+"</td><td class=\"num\">"+g_pw+"</td><td class=\"num\">"+std::to_string(gm)+"</td><td class=\"num\">"+std::to_string(gy)+"</td><td class=\"num\">"+g_py+"</td></tr>";
        }
      }
      html+="</tbody><tfoot><tr><td colspan=\"2\">Total Consolidado</td><td class=\"num\">"+std::to_string(total_uacs)+"</td><td class=\"num\">"+std::to_string(total_attendances)+"</td><td class=\"num\">"+std::to_string(total_women)+"</td><td class=\"num\">"+pct_women+"</td><td class=\"num\">"+std::to_string(total_men)+"</td><td class=\"num\">"+std::to_string(total_youth)+"</td><td class=\"num\">"+pct_youth+"</td></tr></tfoot></table>";

      html+="<h2>📍 Detalhamento Territorial por Unidade de Atendimento (UAC)</h2>";
      html+="<table><thead><tr><th>Grupo</th><th>UAC / Unidade</th><th>Município (IBGE)</th><th>COREDE</th><th>Região Funcional</th><th>Bioma</th><th class=\"num\">Total</th><th class=\"num\">Mulheres</th><th class=\"num\">Homens</th><th class=\"num\">Jovens</th><th>Fonte / Qualidade</th></tr></thead><tbody>";
      if(data.empty()){
        html+="<tr><td colspan=\"11\" style=\"text-align:center;color:#64748b;padding:1.5rem\">Nenhum registro de participação consolidado no recorte selecionado.</td></tr>";
      }else{
        for(auto&x:data){
          std::string ibge_str=x.value("ibge_code","");
          html+="<tr><td><span class=\"tag\">"+esc(x.value("group_code",""))+"</span></td>";
          html+="<td><strong>"+esc(x.value("unit_name",""))+"</strong></td>";
          html+="<td>"+esc(x.value("municipality",""))+(ibge_str.empty()?"":" <small style=\"color:#64748b\">("+esc(ibge_str)+")</small>")+"</td>";
          html+="<td>"+esc(x.value("corede","—"))+"</td>";
          html+="<td>"+esc(x.value("functional_region","—"))+"</td>";
          html+="<td><span class=\"tag tag-green\">"+esc(x.value("biome_predominant","—"))+"</span></td>";
          html+="<td class=\"num\"><strong>"+std::to_string(x.value("total",0))+"</strong></td>";
          html+="<td class=\"num\">"+std::to_string(x.value("women",0))+"</td>";
          html+="<td class=\"num\">"+std::to_string(x.value("men",0))+"</td>";
          html+="<td class=\"num\">"+std::to_string(x.value("youth",0))+"</td>";
          html+="<td><small>"+esc(x.value("source_locator",""))+"</small></td></tr>";
        }
      }
      html+="</tbody><tfoot><tr><td colspan=\"6\">Total do Recorte</td><td class=\"num\">"+std::to_string(total_attendances)+"</td><td class=\"num\">"+std::to_string(total_women)+"</td><td class=\"num\">"+std::to_string(total_men)+"</td><td class=\"num\">"+std::to_string(total_youth)+"</td><td>—</td></tr></tfoot></table>";

      if(!action_items.empty()){
        html+="<h2>📋 Encaminhamentos e Ações Registradas</h2>";
        html+="<table><thead><tr><th>Título</th><th>Unidade</th><th>Prioridade</th><th>Status</th><th>Prazo</th><th>Detalhes</th></tr></thead><tbody>";
        for(auto&ai:action_items){
          std::string st_class=ai.value("status","")=="completed"?"tag-green":ai.value("status","")=="in_progress"?"tag-blue":"tag-amber";
          html+="<tr><td><strong>"+esc(ai.value("title",""))+"</strong></td><td>"+esc(ai.value("unit_name","—"))+"</td><td><span class=\"tag\">"+esc(ai.value("priority","normal"))+"</span></td><td><span class=\"tag "+st_class+"\">"+esc(ai.value("status",""))+"</span></td><td>"+esc(ai.value("due_at","—"))+"</td><td><small>"+esc(ai.value("details",""))+"</small></td></tr>";
        }
        html+="</tbody></table>";
      }

      if(!observations.empty()){
        html+="<h2>🔍 Observações e Evidências de Campo</h2>";
        html+="<table><thead><tr><th>Tema</th><th>Declaração</th><th>Unidade</th><th>Tipo</th><th>Status Epistêmico</th><th>Validação</th></tr></thead><tbody>";
        for(auto&ob:observations){
          std::string v_class=ob.value("validation_status","")=="validated"?"tag-green":ob.value("validation_status","")=="review_required"?"tag-amber":"tag-blue";
          html+="<tr><td><strong>"+esc(ob.value("topic",""))+"</strong></td><td>"+esc(ob.value("statement",""))+"</td><td>"+esc(ob.value("unit_name","—"))+"</td><td><span class=\"tag\">"+esc(ob.value("observation_kind",""))+"</span></td><td>"+esc(ob.value("epistemic_status",""))+"</td><td><span class=\"tag "+v_class+"\">"+esc(ob.value("validation_status",""))+"</span></td></tr>";
        }
        html+="</tbody></table>";
      }

      html+="<div class=\"provenance-box\">";
      html+="<h3 style=\"margin:0 0 0.4rem 0;color:#0369a1;font-size:0.95rem;\">📌 Proveniência dos Dados e Ressalvas Metodológicas</h3>";
      html+="<p style=\"margin:0 0 0.4rem 0;\"><strong>Catálogo e Integração Territorial:</strong> Os recortes territoriais (Município, COREDE, Região Funcional e Biomas) foram alinhados com o serviço de referência TRAMA-RS e mapeamento IBGE.</p>";
      html+="<p style=\"margin:0 0 0.4rem 0;\"><strong>Contagem de Participações:</strong> As participações informadas correspondem ao somatório de presenças em atividades documentadas e podem não equivaler a indivíduos únicos em caso de múltiplas oficinas.</p>";
      html+="<p style=\"margin:0;\"><strong>Validação de Impacto:</strong> Este relatório consolida evidências de monitoramento operacional e governança técnica; inferências de causalidade ecológica ou econômica requerem amostragem e desenhos quase-experimentais dedicados.</p>";
      html+="</div>";

      if(inc_map){
        json units_json=json::array();
        for(auto&x:data){
          if(!x["latitude"].is_null()&&!x["longitude"].is_null()){
            units_json.push_back({
              {"unit_name",x.value("unit_name","")},
              {"municipality",x.value("municipality","")},
              {"group_code",x.value("group_code","")},
              {"ibge_code",x.value("ibge_code","")},
              {"corede",x.value("corede","")},
              {"functional_region",x.value("functional_region","")},
              {"biome_predominant",x.value("biome_predominant","")},
              {"total",x.value("total",0)},
              {"women",x.value("women",0)},
              {"men",x.value("men",0)},
              {"youth",x.value("youth",0)},
              {"latitude",x["latitude"].get<double>()},
              {"longitude",x["longitude"].get<double>()}
            });
          }
        }
        html+="<script>";
        html+="document.addEventListener('DOMContentLoaded', function() {";
        html+="  var units = "+units_json.dump()+";";
        html+="  var mapEl = document.getElementById('report-map');";
        html+="  if (!mapEl) return;";
        html+="  if (typeof L !== 'undefined') {";
        html+="    try {";
        html+="      var map = L.map('report-map', { attributionControl: false }).setView([-30.0, -53.0], 6);";
        html+="      L.tileLayer('https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png', { maxZoom: 18 }).addTo(map);";
        html+="      var markers = [];";
        html+="      units.forEach(function(u) {";
        html+="        if (typeof u.latitude === 'number' && typeof u.longitude === 'number') {";
        html+="          var marker = L.circleMarker([u.latitude, u.longitude], {";
        html+="            radius: 8, fillColor: '#0e503e', color: '#ffffff', weight: 2, opacity: 1, fillOpacity: 0.85";
        html+="          }).addTo(map);";
        html+="          marker.bindPopup('<div style=\"font-family:sans-serif;font-size:13px;line-height:1.4;\"><strong>' + u.unit_name + '</strong><br/>Município: <b>' + u.municipality + '</b><br/>Grupo: <b>' + u.group_code + '</b> · COREDE: ' + (u.corede || '—') + '<br/><hr style=\"margin:4px 0;border:0;border-top:1px solid #e2e8f0;\"/>Total: <b>' + u.total + '</b> (Mulheres: ' + u.women + ', Homens: ' + u.men + ', Jovens: ' + u.youth + ')</div>');";
        html+="          markers.push(marker);";
        html+="        }";
        html+="      });";
        html+="      if (markers.length > 0) {";
        html+="        var group = L.featureGroup(markers);";
        html+="        map.fitBounds(group.getBounds().pad(0.15));";
        html+="      }";
        html+="      var mapLayer = '"+esc(map_layer)+"';";
        html+="      var mapMetric = '"+esc(map_metric)+"';";
        html+="      if (mapLayer) {";
        html+="        fetch('/api/v1/maps/' + encodeURIComponent(mapLayer)).then(function(res) { return res.ok ? res.json() : null; }).then(function(geo) {";
        html+="          if (!geo) return;";
        html+="          var totals = {};";
        html+="          units.forEach(function(u) {";
        html+="            var key = mapLayer === 'municipios' ? u.municipality : mapLayer === 'coredes' ? u.corede : mapLayer === 'regioes-funcionais' ? u.functional_region : u.biome_predominant;";
        html+="            if (key) { totals[key] = (totals[key] || 0) + (u[mapMetric] || 0); }";
        html+="          });";
        html+="          var vals = Object.values(totals); var max = Math.max.apply(null, vals.concat([1]));";
        html+="          function getColor(v) {";
        html+="            if (v === undefined) return '#e4e8e5';";
        html+="            if (v === 0) return '#d6e8df';";
        html+="            if (v <= max * 0.25) return '#b8ddcc';";
        html+="            if (v <= max * 0.50) return '#73b99c';";
        html+="            if (v <= max * 0.75) return '#318267';";
        html+="            return '#0e503e';";
        html+="          }";
        html+="          var thematic = L.geoJSON(geo, {";
        html+="            style: function(f) {";
        html+="              var k = mapLayer === 'biomas' ? f.properties.id : f.properties.nome;";
        html+="              return { color: '#557068', weight: 1, fillColor: getColor(totals[k]), fillOpacity: 0.65 };";
        html+="            }";
        html+="          }).addTo(map);";
        html+="          thematic.bringToBack();";
        html+="        }).catch(function() {});";
        html+="      }";
        html+="    } catch(e) { console.error('Map init error:', e); }";
        html+="  }";
        html+="});";
        html+="</script>";
      }

      html+="</div></body></html>";

      St s(d,"INSERT INTO report_runs VALUES(?,?,?,?,?,?,?,?)");
      s.b(1,id);
      s.b(2,pid);
      s.b(3,"technical_html");
      s.b(4,params.dump());
      s.b(5,now());
      s.b(6,u->id);
      s.b(7,now());
      s.b(8,"attendance-v1");
      s.row();

      z.set_header("X-Report-Id",id);
      z.set_content(html,"text/html; charset=utf-8");
      headers(z);
    }catch(const std::exception&e){
      error(z,422,"report_error",e.what());
    }
  };
  app.Get("/api/v1/reports/preview",handle_report);
  app.Post("/api/v1/reports/preview",handle_report);
 app.Get("/api/v1/admin/audit",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z,false,true))return;respond(z,{{"data",rows(d,"SELECT id,actor_id,project_id,entity_type,entity_id,action,event_at,event_hash FROM audit_events ORDER BY event_at DESC LIMIT 200")}});});
 app.Get("/api/v1/admin/integrations",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z,false,true))return;json a=json::array();try{auto health=rs_get("/v1/health");a.push_back({{"id","trama-rs"},{"name","TRAMA-RS"},{"status","integrated"},{"health",health["data"]["healthy"]},{"endpoint","http://10.163.80.176:8080"},{"validation_status",health["data"]["status"]}});}catch(const std::exception&e){a.push_back({{"id","trama-rs"},{"name","TRAMA-RS"},{"status","unavailable"},{"health",false},{"error",e.what()}});}for(auto name:{"ENTE","Morfocampo","SYNTH","JEV","SisterSTRATA","ELO","SisTer-Nexo"})a.push_back({{"id",slug(name)},{"name",name},{"status","not_integrated"},{"health","not_configured"}});respond(z,{{"data",a}});});
 app.Get(R"(/(.*))",[p](const httplib::Request&r,httplib::Response&z){auto rel=r.matches[1].str();if(rel.empty())rel="index.html";if(rel.find("..")!=std::string::npos)return error(z,400,"invalid_path","Caminho inválido");auto file=p.web/rel;if(!fs::exists(file)||!fs::is_regular_file(file))file=p.web/"index.html";auto ext=file.extension().string();std::string mime=ext==".js"?"text/javascript":ext==".css"?"text/css":"text/html";z.set_content(read(file),mime+"; charset=utf-8");headers(z);if(ext==".js"||ext==".css")z.set_header("Cache-Control","public, max-age=3600");else z.set_header("Cache-Control","no-store");});
 int actual_port=port;if(actual_port==0){actual_port=app.bind_to_any_port(host.c_str());if(actual_port<=0)throw std::runtime_error("não foi possível abrir porta livre no host "+host);std::cout<<"TRAMA 0.1.0 em http://"<<host<<":"<<actual_port<<"\n";if(!app.listen_after_bind())throw std::runtime_error("não foi possível iniciar o servidor");}else{std::cout<<"TRAMA 0.1.0 em http://"<<host<<":"<<actual_port<<"\n";if(!app.listen(host,actual_port))throw std::runtime_error("não foi possível abrir host/porta");}return 0;}
}
