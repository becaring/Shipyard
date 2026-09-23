"""Generate a distinct insertion-ordered DOM from the locked bounded backend.

This is a concrete second DOM, not an alias to sorted json. Official ordered_map
methods retain vector<pair<const string,T>> ownership and invalidation behavior.
"""
import hashlib,re
from pathlib import Path
MAP_HASH='2d4e2d9f6e6a0c2d2adadcb620da1f49585fa4e54e54cfa16b6d20e01d857d61'
HEADER_MARKER='#include "json/generated/ordered_json.hpp"\n'
CPP_MARKER='#include "json/generated/ordered_json.cpp"\n'
def method(s,signature):
    start=s.index(signature);opening=s.index('\n    {',start)+5
    return s[start:s.index('\n    }',opening)+6]
def generate(source,out):
    backend=Path(__file__).resolve().parents[3]/'sources/json'
    raw=(source/'include/nlohmann/ordered_map.hpp').read_bytes()
    if hashlib.sha256(raw).hexdigest()!=MAP_HASH:raise ValueError('ordered_map source mismatch')
    s=raw.decode().replace('\r\n','\n')
    methods=[]
    for sig in ['    std::pair<iterator, bool> emplace(const key_type& key, T&& t)',
                '    T& operator[](const key_type& key)','    size_type erase(const key_type& key)',
                '    iterator erase(iterator pos)','    iterator erase(iterator first, iterator last)',
                '    iterator find(const key_type& key)','    const_iterator find(const key_type& key) const',
                '    std::pair<iterator, bool> insert( const value_type& value )']:
        m=method(s,sig)
        m=re.sub(r'return \{([^;]+)\};',r'return std::make_pair(\1);',m)
        m=m.replace('T{}','T()').replace('Container::emplace_back(key, std::forward<T>(t));','Container::push_back(value_type(key, std::forward<T>(t)));')
        m=re.sub(r'new \(&\*it\) value_type\{([^;]+)\};',r'new (&*it) value_type(\1);',m)
        m=m.replace('Container::resize(this->size() - static_cast<size_type>(elements_affected));','for (size_type removed = 0; removed < static_cast<size_type>(elements_affected); ++removed) Container::pop_back();')
        methods.append(m)
    storage='''// Projected from nlohmann 3.12.0 ordered_map.hpp.
// SPDX-FileCopyrightText: 2013 - 2025 Niels Lohmann <https://nlohmann.me>
// SPDX-License-Identifier: MIT
#include <vector>
#include <string>
#include <utility>
#include <functional>
#include <iterator>
#include <new>
namespace nlohmann { namespace ordered_backend {
// XDK eagerly instantiates the invalid assignment in pair<const string,T>.
// The compiler projection retains a const key and normal mapped value while
// rejecting pair assignment; ordered_map erase already reconstructs entries.
#if defined(_XBOX) || defined(SHIPYARD_JSON_ORDERED_ENTRY_PROJECTION)
template<class T> struct OrderedEntry {
    const std::string first;
    T second;
    OrderedEntry() : first(), second() {}
    OrderedEntry(const std::string& k, const T& v):first(k),second(v){}
    OrderedEntry(const std::string& k, T&& v):first(k),second(std::move(v)){}
    OrderedEntry(const OrderedEntry& v):first(v.first),second(v.second){}
    OrderedEntry(OrderedEntry&& v):first(v.first),second(std::move(v.second)){}
    template<class K> OrderedEntry(const std::pair<K,T>& v):first(v.first),second(v.second){}
    bool operator==(const OrderedEntry& v)const{return first==v.first && second==v.second;}
private:
    OrderedEntry& operator=(const OrderedEntry&);
};
template<class T> struct OrderedEntryType { typedef OrderedEntry<T> type; };
#else
template<class T> struct OrderedEntryType { typedef std::pair<const std::string,T> type; };
#endif
template<class T> struct OrderedObject : std::vector<typename OrderedEntryType<T>::type> {
    typedef std::string key_type;
    typedef std::vector<typename OrderedEntryType<T>::type> Container;
    typedef typename Container::iterator iterator;
    typedef typename Container::const_iterator const_iterator;
    typedef typename Container::size_type size_type;
    typedef typename Container::value_type value_type;
    std::equal_to<key_type> m_compare;
    void clear() { while (!this->empty()) Container::pop_back(); }
'''+ '\n'.join(methods)+'\n};\n}}\n'
    h=(backend/'include/nlohmann/json.hpp').read_text().replace(HEADER_MARKER,'')
    h=h.replace('SHIPYARD_JSON_HPP','SHIPYARD_ORDERED_JSON_HPP')
    h=h.replace('namespace nlohmann {','namespace nlohmann { namespace ordered_backend {',1)
    h=h.replace('} // namespace nlohmann','}} // namespace nlohmann::ordered_backend')
    h=h.replace('nlohmann::','nlohmann::ordered_backend::')
    h=h.replace('typedef std::map<std::string, json> object_t;','typedef OrderedObject<json> object_t;')
    begin=h.index('    class exception :');end=h.index('    class json_pointer',begin)
    h=h[:begin]+''.join('    typedef ::nlohmann::json::'+x+' '+x+';\n' for x in ['exception','type_error','out_of_range','invalid_iterator','parse_error'])+h[end:]
    begin=h.index('#if defined(_XBOX)');end=h.index('#endif',begin)+len('#endif')
    h=h[:begin]+'template <class T> struct enum_underlying : ::nlohmann::detail::enum_underlying<T> {};'+h[end:]
    h='#ifndef SHIPYARD_ORDERED_DOM_GENERATED_HPP\n#define SHIPYARD_ORDERED_DOM_GENERATED_HPP\n'+storage+h+'\nnamespace nlohmann { typedef ordered_backend::json ordered_json; }\n#endif\n'
    cpp=(backend/'json.cpp').read_text().replace(CPP_MARKER,'')
    cpp=cpp.replace('namespace nlohmann {','namespace nlohmann { namespace ordered_backend {',1).replace('} // namespace nlohmann','}} // namespace nlohmann::ordered_backend')
    begin=cpp.index('static std::string Prefix(');end=cpp.index('json::json(const json &o)',begin)
    cpp=cpp[:begin]+cpp[end:]
    cpp=cpp.replace('result=pos; ++result; object_->erase(pos.object_);','result.object_=object_->erase(pos.object_);')
    results={}
    for name,text in [('ordered_json.hpp',h),('ordered_json.cpp',cpp)]:
        path=out/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_text(text,newline='\n')
        results[name]=hashlib.sha256(path.read_bytes()).hexdigest()
    return {'ordered_map_source_sha256':MAP_HASH,'generated_sha256':results,'object_storage':'vector of const-key entries; std::pair on standard host, OrderedEntry on XDK','distinct_from_sorted_json':True,'cross_dom_conversion':False,'initializer_lists':False}
