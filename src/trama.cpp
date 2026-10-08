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
void headers(httplib::Response&r){r.set_header("X-Content-Type-Options","nosniff");r.set_header("X-Frame-Options","DENY");r.set_header("Referrer-Policy","no-referrer");r.set_header("Content-Security-Policy","default-src 'self'; style-src 'self' 'unsafe-inline'; script-src 'self'; img-src 'self' data:; frame-ancestors 'none'");}
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
int cli(int argc,char**argv){if(argc<2){std::cout<<"TRAMA 0.1.0\nUso: trama <init|setup-admin|doctor|verify|backup|restore|version> [opções]\n";return 2;}std::string cmd=argv[1],root=arg(argc,argv,"--data-dir","var"),source=arg(argc,argv,"--source-dir",fs::current_path().string());auto p=paths(root,source);if(cmd=="version"){std::cout<<"TRAMA 0.1.0 (C++ "<<__cplusplus<<", SQLite "<<sqlite3_libversion()<<")\n";return 0;}if(cmd=="init"){initialize(p);std::cout<<"Banco inicializado: "<<p.db<<"\n";}else if(cmd=="setup-admin"){std::string login=arg(argc,argv,"--login","admin"),display=arg(argc,argv,"--display-name","Administrador"),pass=arg(argc,argv,"--password");if(pass.empty()){if(!isatty(STDIN_FILENO))throw std::runtime_error("password prompt requires a terminal");char*raw=getpass("Senha (mínimo 12 caracteres): ");pass=raw?raw:"";}setup_admin(p,login,display,pass);std::cout<<"Administrador criado.\n";}else if(cmd=="verify"||cmd=="doctor"){auto j=verify(p);j["compiler"]="GCC "+std::to_string(__GNUC__)+"."+std::to_string(__GNUC_MINOR__);j["mode"]="local-loopback-ready";j["lan_ready"]=false;j["lan_note"]="HTTPS/certificado não configurado";std::cout<<j.dump(2)<<"\n";return j["ok"]?0:1;}else if(cmd=="backup"){auto out=arg(argc,argv,"--output");if(out.empty())throw std::runtime_error("--output is required");backup(p,out);std::cout<<"Backup criado: "<<out<<"\n";}else if(cmd=="restore"){auto in=arg(argc,argv,"--input");if(in.empty())throw std::runtime_error("--input is required");fs::create_directories(p.root);Db src(in),dst(p.db);sqlite3_backup*b=sqlite3_backup_init(dst.p,"main",src.p,"main");if(!b)throw std::runtime_error(sqlite3_errmsg(dst.p));int rc=sqlite3_backup_step(b,-1);sqlite3_backup_finish(b);if(rc!=SQLITE_DONE)throw std::runtime_error("restore failed");std::cout<<"Restauração concluída.\n";}else throw std::runtime_error("unknown command: "+cmd);return 0;}

int server_main(int argc,char**argv){std::string root=arg(argc,argv,"--data-dir","var"),source=arg(argc,argv,"--source-dir",fs::current_path().string()),host=arg(argc,argv,"--host","127.0.0.1");int port=std::stoi(arg(argc,argv,"--port","8088"));if(host!="127.0.0.1"&&host!="localhost"&&host!="::1")throw std::runtime_error("HTTP sem TLS só pode escutar em loopback; configure HTTPS antes de usar LAN");auto p=paths(root,source);initialize(p);try{auto sync=sync_territories(p);std::cout<<"TRAMA-RS sincronizado: "<<sync["updated"]<<" unidades atualizadas, "<<sync["unmatched"]<<" não conciliadas\n";}catch(const std::exception&e){std::cerr<<"Aviso: sincronização TRAMA-RS indisponível: "<<e.what()<<"\n";}Db check(p.db);if(!fs::exists(p.web/"index.html"))throw std::runtime_error("frontend ausente; execute ./scripts/build.sh");httplib::Server app;app.set_payload_max_length(20*1024*1024);app.set_read_timeout(15,0);app.set_write_timeout(30,0);app.set_error_handler([](const httplib::Request&,httplib::Response&r){if(r.status>=400&&r.get_header_value("Content-Type").empty())error(r,r.status,"http_error","Requisição não atendida");});app.Get("/healthz",[](const auto&,auto&r){respond(r,{{"status","ok"},{"service","trama"}});});app.Get("/readyz",[p](const auto&,auto&r){try{auto v=verify(p);if(!v["ok"].get<bool>())return error(r,503,"not_ready","Banco não íntegro");respond(r,{{"status","ready"},{"database","ok"},{"static_files",fs::exists(p.web/"index.html")}});}catch(...){error(r,503,"not_ready","Serviço indisponível");}});
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
 app.Get("/api/v1/exports/attendance.json",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;auto data=rows(d,"SELECT group_code,unit_name,municipality,total,women,men,youth,source_locator FROM v_attendance WHERE project_id=? ORDER BY unit_name",[&](St&s){s.b(1,r.get_param_value("project_id"));});respond(z,{{"metadata",{{"generated_at",now()},{"rule_version","attendance-v1"},{"provenance","document_aggregate"}}},{"data",data}});});
 app.Get("/api/v1/exports/attendance.csv",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z))return;auto data=rows(d,"SELECT group_code,unit_name,municipality,total,women,men,youth,source_locator FROM v_attendance WHERE project_id=? ORDER BY unit_name",[&](St&s){s.b(1,r.get_param_value("project_id"));});auto cell=[](std::string v){if(!v.empty()&&std::string("=+-@").find(v[0])!=std::string::npos)v="'"+v;size_t pos=0;while((pos=v.find('"',pos))!=std::string::npos){v.insert(pos,"\"");pos+=2;}return "\""+v+"\"";};std::string csv="\xEF\xBB\xBFgrupo,uac,municipio,total,mulheres,homens,jovens,fonte\r\n";for(auto&x:data)csv+=cell(x["group_code"])+","+cell(x["unit_name"])+","+cell(x["municipality"])+","+std::to_string((int)x["total"])+","+std::to_string((int)x["women"])+","+std::to_string((int)x["men"])+","+std::to_string((int)x["youth"])+","+cell(x["source_locator"])+"\r\n";z.set_header("Content-Disposition","attachment; filename=trama-participacoes.csv");z.set_content(csv,"text/csv; charset=utf-8");headers(z);});
 app.Post("/api/v1/reports/preview",[p](const httplib::Request&r,httplib::Response&z){try{Db d(p.db);auto u=require(d,r,z,true);if(!u)return;auto j=json::parse(r.body); std::string pid=j.value("project_id","");auto data=rows(d,"SELECT group_code,unit_name,municipality,total,women,men,youth FROM v_attendance WHERE project_id=? ORDER BY group_code,unit_name",[&](St&s){s.b(1,pid);});auto ov=overview(p,{},{},pid);std::string id="report-"+random_hex(12),html="<!doctype html><html lang=pt-BR><meta charset=utf-8><title>Relatório TRAMA</title><style>body{font:16px system-ui;max-width:1000px;margin:auto;padding:2rem}table{border-collapse:collapse;width:100%}th,td{padding:.5rem;border:1px solid #bbb;text-align:left}@media print{button{display:none}}</style><button onclick=print()>Imprimir / salvar como PDF</button><h1>Relatório técnico TRAMA</h1><p>Gerado em "+now()+" · regra attendance-v1</p><h2>Sumário</h2><p>Participações informadas: "+std::to_string((int)ov["kpis"]["reported_attendances"])+"; mulheres: "+std::to_string((int)ov["kpis"]["reported_women"])+"; homens: "+std::to_string((int)ov["kpis"]["reported_men"])+"; jovens: "+std::to_string((int)ov["kpis"]["reported_youth"])+".</p><table><thead><tr><th>Grupo<th>UAC<th>Município<th>Total<th>Mulheres<th>Homens<th>Jovens</tr></thead><tbody>";for(auto&x:data)html+="<tr><td>"+esc(x["group_code"])+"<td>"+esc(x["unit_name"])+"<td>"+esc(x["municipality"])+"<td>"+std::to_string((int)x["total"])+"<td>"+std::to_string((int)x["women"])+"<td>"+std::to_string((int)x["men"])+"<td>"+std::to_string((int)x["youth"]);html+="</tbody></table><h2>Fonte e ressalvas</h2><p>Consulte as fontes vinculadas aos registros. Participações podem não equivaler a pessoas únicas. Categorias sobrepostas não devem ser somadas ao total. Este relatório não demonstra impacto sem medições adequadas.</p></html>";St s(d,"INSERT INTO report_runs VALUES(?,?,?,?,?,?,?,?)");s.b(1,id);s.b(2,pid);s.b(3,"technical_html");s.b(4,j.dump());s.b(5,now());s.b(6,u->id);s.b(7,now());s.b(8,"attendance-v1");s.row();z.set_header("X-Report-Id",id);z.set_content(html,"text/html; charset=utf-8");headers(z);}catch(const std::exception&e){error(z,422,"report_error",e.what());}});
 app.Get("/api/v1/admin/audit",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z,false,true))return;respond(z,{{"data",rows(d,"SELECT id,actor_id,project_id,entity_type,entity_id,action,event_at,event_hash FROM audit_events ORDER BY event_at DESC LIMIT 200")}});});
 app.Get("/api/v1/admin/integrations",[p](const httplib::Request&r,httplib::Response&z){Db d(p.db);if(!require(d,r,z,false,true))return;json a=json::array();try{auto health=rs_get("/v1/health");a.push_back({{"id","trama-rs"},{"name","TRAMA-RS"},{"status","integrated"},{"health",health["data"]["healthy"]},{"endpoint","http://10.163.80.176:8080"},{"validation_status",health["data"]["status"]}});}catch(const std::exception&e){a.push_back({{"id","trama-rs"},{"name","TRAMA-RS"},{"status","unavailable"},{"health",false},{"error",e.what()}});}for(auto name:{"ENTE","Morfocampo","SYNTH","JEV","SisterSTRATA","ELO","SisTer-Nexo"})a.push_back({{"id",slug(name)},{"name",name},{"status","not_integrated"},{"health","not_configured"}});respond(z,{{"data",a}});});
 app.Get(R"(/(.*))",[p](const httplib::Request&r,httplib::Response&z){auto rel=r.matches[1].str();if(rel.empty())rel="index.html";if(rel.find("..")!=std::string::npos)return error(z,400,"invalid_path","Caminho inválido");auto file=p.web/rel;if(!fs::exists(file)||!fs::is_regular_file(file))file=p.web/"index.html";auto ext=file.extension().string();std::string mime=ext==".js"?"text/javascript":ext==".css"?"text/css":"text/html";z.set_content(read(file),mime+"; charset=utf-8");headers(z);if(ext==".js"||ext==".css")z.set_header("Cache-Control","public, max-age=3600");else z.set_header("Cache-Control","no-store");});
 std::cout<<"TRAMA 0.1.0 em http://"<<host<<":"<<port<<"\n";if(!app.listen(host,port))throw std::runtime_error("não foi possível abrir host/porta");return 0;}
}
