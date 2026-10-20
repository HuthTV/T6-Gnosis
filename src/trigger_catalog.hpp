#pragma once
#include "navigation.hpp"
#include <string_view>
namespace triggers {
struct CatalogEntry {const char* map;const char* field;const char* value;const char* reference;const char* classname;nav::Vec3 origin,dimensions;};
struct ScriptField {std::string key,value;};
inline bool catalog_scope(std::string_view scope,std::string_view bsp) {
 return scope=="*"||bsp==std::string("maps/mp/")+std::string(scope)+".d3dbsp";
}
inline std::string_view catalog_key(std::string_view key) {return key.substr(0,5)=="stub."?key.substr(5):key;}
inline bool metadata_key(std::string_view key) {
 key=catalog_key(key);return key=="zone_enabled"||key=="monitor_override"||key=="targetname"||key=="script_noteworthy"||key=="target"||key=="script_string"||key=="unitrigger_system";
}
inline bool metadata_match(const CatalogEntry& entry,const std::vector<ScriptField>& fields,std::string_view map) {
 if(!entry.field[0]||!catalog_scope(entry.map,map))return false;
 for(const auto& f:fields)if(catalog_key(f.key)==entry.field&&f.value==entry.value)return true;
 return false;
}
inline bool geometry_candidate(const CatalogEntry& entry,std::string_view map,std::string_view classname,nav::Vec3 origin,nav::Vec3 dimensions) {
 if(!entry.classname[0]||!catalog_scope(entry.map,map)||classname!=entry.classname||!nav::finite(origin)||!nav::finite(dimensions))return false;
 const auto close=[](nav::Vec3 a,nav::Vec3 b){return std::abs(a.x-b.x)<0.02f&&std::abs(a.y-b.y)<0.02f&&std::abs(a.z-b.z)<0.02f;};
 return close(origin,entry.origin)&&close(dimensions,entry.dimensions);
}
}
