#pragma once
#include <stdint.h>
#include <vector>
#include <iterator>
#include <string>
#include <cctype>

template<class T> auto PrismBegin(T& value)->decltype(value.begin()){return value.begin();}
template<class T> auto PrismEnd(T& value)->decltype(value.end()){return value.end();}
template<class T,size_t N> T* PrismBegin(T (&value)[N]){return value;}
template<class T,size_t N> T* PrismEnd(T (&value)[N]){return value+N;}

inline std::vector<size_t> PrismDimensions(size_t a){return std::vector<size_t>(1,a);}
inline std::vector<size_t> PrismDimensions(size_t a,size_t b){size_t v[]={a,b};return std::vector<size_t>(v,v+2);}
inline std::vector<size_t> PrismDimensions(size_t a,size_t b,size_t c){size_t v[]={a,b,c};return std::vector<size_t>(v,v+3);}
inline std::vector<size_t> PrismDimensions(size_t a,size_t b,size_t c,size_t d){size_t v[]={a,b,c,d};return std::vector<size_t>(v,v+4);}
