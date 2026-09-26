#include "resources/path_hash.h"
#include <cassert>
#include <cstdio>
#include <map>
#include <unordered_map>
#include <vector>

struct SampledHash {
    size_t operator()(const std::string& path) const {
        uint32_t value=2166136261u;
        for(size_t i=0;i<path.size();i+=1+path.size()/10)
            value=(value*16777619u)^static_cast<unsigned char>(path[i]);
        return value;
    }
};
static unsigned comparisons;
struct Equal {
    bool operator()(const std::string& a,const std::string& b) const {
        ++comparisons; return a==b;
    }
};
template<class Hash> unsigned Exercise(const std::vector<std::string>& paths) {
    std::unordered_map<std::string,unsigned,Hash,Equal> cache;
    cache.reserve(paths.size()*2);
    for(unsigned i=0;i<paths.size();++i) cache[paths[i]]=i;
    comparisons=0;
    for(unsigned i=0;i<paths.size();++i) assert(cache.find(paths[i])->second==i);
    unsigned work=comparisons;
    cache.erase(paths[0]); assert(cache.find(paths[0])==cache.end());
    cache[paths[0]]=123; assert(cache.find(paths[0])->second==123);
    assert(cache.find("missing")==cache.end());
    return work;
}
int main() {
    std::vector<std::string> paths;
    for(unsigned i=0;i<4096;++i) {
        char path[80];
        std::sprintf(path,"textures/kanji/gMsgKanji%04XTex",0x8000+i);
        paths.push_back(path);
    }
    unsigned before=Exercise<SampledHash>(paths);
    unsigned after=Exercise<Shipyard::Resources::PathHash>(paths);
    std::printf("path equality comparisons: %u -> %u\n",before,after);
    assert(after*20<before);
    Shipyard::Resources::PathHash hash;
    assert(hash(std::string("abc"))==hash(std::string("abc")));
    assert(hash(std::string("a\0b",3))!=hash(std::string("a\0c",3)));
    assert(hash(std::string())==2166136261u);
    std::puts("path identity, collision reduction, erase/reload and misses: pass");
}
