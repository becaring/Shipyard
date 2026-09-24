"""Shared pinned spdlog compiler projection for both Xbox 360 consumers."""
from pathlib import Path
import re,json,hashlib
UP=BASE=LANG=PRISM=None
NAMESPACE=ENV_HEADER=ENV_GET=GUARD_SOURCE=GUARD_TARGET=None
OVERLOAD_MODE=None

def configure(*, upstream, base, language, syntax, namespace, environment_header,
              environment_get, guard_source, guard_target, overload_mode):
    """Bind consumer source and ABI policy before common() or generate()."""
    if overload_mode not in ('filtered','eager'):
        raise ValueError('unknown logging conversion policy')
    global UP,BASE,LANG,PRISM,NAMESPACE,ENV_HEADER,ENV_GET,GUARD_SOURCE,GUARD_TARGET,OVERLOAD_MODE
    UP,BASE,LANG,PRISM=Path(upstream),Path(base),language,syntax
    NAMESPACE,ENV_HEADER,ENV_GET=namespace,Path(environment_header),environment_get
    GUARD_SOURCE,GUARD_TARGET,OVERLOAD_MODE=guard_source,guard_target,overload_mode
def exact(t,a,b,count=1):
    if t.count(a)!=count:raise ValueError('core match failed '+a[:100])
    return t.replace(a,b)
def read(name):
    p=UP/'spdlog'/name;raw=p.read_bytes();locks=json.loads((BASE/'core-source-lock.json').read_text())
    if hashlib.sha256(raw).hexdigest()!=locks[name]:raise ValueError('core source changed '+name)
    return raw.decode().replace('\r\n','\n')

def large_format_overloads(t,free_functions=False,mode='filtered'):
    # Filtered mode retains modern's conversion gate; eager mode retains the
    # Smash formatter ABI and its original short-arity fmt4 macro.
    if mode not in ('filtered','eager'):
        raise ValueError('unknown logging conversion policy')
    if mode=='eager':
        def expand_eager(match):
            name=match.group(1);fixed=match.group(2).split(',');result=match.group(0)+'\n'
            for n in range(16,40):
                types=','.join('class A%d'%i for i in range(n))
                params=','.join('%s p%d'%(typ,i) for i,typ in enumerate(fixed))
                params+=','+','.join('const A%d& a%d'%(i,i) for i in range(n))
                result+='template<'+types+'> inline void '+name+'('+params+'){\n'
                result+='fmt::internal::ArgArray<%d>::Type arr;\n'%n
                for i in range(n):result+='arr[%d]=fmt::internal::MakeArg<fmt::BasicFormatter<char> >(a%d);\n'%(i,i)
                result+='arr[%d].type=fmt::internal::Arg::NONE;\nfmt::ULongLong types=0;\n'%n
                result+='for(unsigned i=0;i<16;++i)types|=static_cast<fmt::ULongLong>(arr[i].type)<<(i*4);\n'
                result+=name+'('+','.join('p%d'%i for i in range(len(fixed)))+',fmt::ArgList(types,arr));}\n'
            return result
        return re.sub(r'FMT_VARIADIC\(void,(\w+),([^\)]+)\)',expand_eager,t)
    def expand(match):
        name=match.group(1);fixed=match.group(2).split(',');result=''
        for n in range(40):
            types=','.join('class A%d'%i for i in range(n))
            params=','.join('%s p%d'%(typ,i) for i,typ in enumerate(fixed))
            args=','.join('p%d'%i for i in range(len(fixed)))
            if n:
                params+=','+','.join('const A%d& a%d'%(i,i) for i in range(n))
                result+='template<'+types+'> '
            result+='inline void '+name+'('+params+'){\n'
            if free_functions:
                values=','+','.join('a%d'%i for i in range(n)) if n else ''
                result+='default_logger_raw()->'+name+'('+args+values+');}\n'
                continue
            level='p%d'%fixed.index('level::level_enum') if name=='log' else 'level::'+{'error':'err'}.get(name,name)
            location='p0' if fixed[0]=='source_loc' else 'source_loc()'
            result+='if(!should_log('+level+') && !should_backtrace()) return;\nSPDLOG_TRY {\n'
            if n:
                result+='fmt::internal::ArgArray<%d>::Type arr;\n'%n
                for i in range(n):
                    kind='MakeValue' if n<16 else 'MakeArg'
                    result+='arr[%d]=fmt::internal::%s<fmt::BasicFormatter<char> >(a%d);\n'%(i,kind,i)
                result+='fmt::ULongLong types=0;\n'
                if n<16:
                    for i in range(n):result+='types|=static_cast<fmt::ULongLong>(fmt::internal::make_type(a%d))<<%d;\n'%(i,i*4)
                else:
                    result+='arr[%d].type=fmt::internal::Arg::NONE;\n'%n
                    result+='for(unsigned i=0;i<16;++i)types|=static_cast<fmt::ULongLong>(arr[i].type)<<(i*4);\n'
                result+=name+'('+args+',fmt::ArgList(types,arr));\n'
            else:result+=name+'('+args+',fmt::ArgList());\n'
            result+='} SPDLOG_LOGGER_CATCH('+location+') }\n'
        return result
    return re.sub(r'FMT_VARIADIC\(void,(\w+),([^\)]+)\)',expand,t)

def generic(t):
    t=t.replace('Shipyard::Logging::',NAMESPACE+'::')
    t=t.replace('SPDLOG_NOEXCEPT','throw()').replace('SPDLOG_TRY','try')
    for inc in ('atomic','chrono','condition_variable','mutex','thread','initializer_list'):t=re.sub(r'^\s*#include <'+inc+r'>\s*$', '',t,flags=re.M)
    t=t.replace('std::chrono::',NAMESPACE+'::chrono::')
    for token in ('lock_guard','unique_lock','mutex','condition_variable'):t=t.replace('std::'+token,NAMESPACE+'::'+token)
    t=t.replace('std::atomic<','Shipyard::Atomic::atomic<')
    t=t.replace('std::recursive_mutex',NAMESPACE+'::logging_recursive_mutex')
    t=t.replace('std::memory_order_relaxed','').replace('std::memory_order_acquire','').replace('std::memory_order_release','')
    t=t.replace(', )',')').replace(',)',')')
    t=re.sub(r'\busing\s+(\w+)\s*=\s*([^;]+);',r'typedef \2 \1;',t)
    t=re.sub(r'\b(?:override|final)\b','',t)
    t=t.replace('[[noreturn]]','__declspec(noreturn)').replace('noexcept','throw()')
    t=PRISM.enum_wrappers(t)
    t=t.replace('(Value v) : value(v)','(Value v=static_cast<Value>(0)) : value(v)')
    t=PRISM.braces(t)
    t=LANG.lower_ranges(t.encode(),begin=NAMESPACE+'::LoggingBegin',end=NAMESPACE+'::LoggingEnd').decode()
    t=t.replace('std::begin(',NAMESPACE+'::LoggingBegin(').replace('std::end(',NAMESPACE+'::LoggingEnd(')
    return t
def common():
    t=read('common.h');start=t.index('#define SPDLOG_LEVEL_TRACE');end=t.index('// make_unique support for pre c++14')
    body=t[start:end]
    body=body.replace('SPDLOG_CONSTEXPR source_loc() = default;','SPDLOG_CONSTEXPR source_loc() : filename(0),line(0),funcname(0) {}')
    for a,b in [('filename{filename_in}','filename(filename_in)'),('line{line_in}','line(line_in)'),('funcname{funcname_in}','funcname(funcname_in)'),('filename{nullptr}','filename'),('line{0}','line'),('funcname{nullptr}','funcname')]:body=body.replace(a,b)
    body=body.replace('enum level_enum : int','enum level_enum')
    declarations='''#pragma once
#include "'''+(BASE/'format_types.h').as_posix()+'''"
#if defined(SPDLOG_USE_STD_FORMAT) || defined(SPDLOG_NO_EXCEPTIONS) || defined(SPDLOG_WCHAR_FILENAMES) || defined(SPDLOG_WCHAR_TO_UTF8_SUPPORT)
#error This XDK logging projection requires narrow fmt formatting and exceptions
#endif
#define SPDLOG_API
#define SPDLOG_HEADER_ONLY
#define SPDLOG_INLINE inline
#define SPDLOG_NOEXCEPT throw()
#define SPDLOG_CONSTEXPR
#define SPDLOG_CONSTEXPR_FUNC inline
#define SPDLOG_FMT_STRING(x) x
#define SPDLOG_FMT_RUNTIME(x) x
#define SPDLOG_FILENAME_T(x) x
#define SPDLOG_BUF_TO_STRING(x) std::string((x).data(),(x).size())
#define SPDLOG_NO_TLS 1
#ifndef SPDLOG_EOL
#define SPDLOG_EOL "\\n"
#endif
#define SPDLOG_TRY try
#define SPDLOG_THROW(ex) throw(ex)
#define SPDLOG_CATCH_STD catch(const std::exception&) {}
#define SPDLOG_FUNCTION static_cast<const char*>(__FUNCTION__)
namespace spdlog {
class formatter;namespace sinks { class sink; }
namespace fmt_lib=fmt;
typedef std::string filename_t;
typedef Shipyard::Logging::chrono::system_clock log_clock;
typedef std::shared_ptr<sinks::sink> sink_ptr;
typedef std::vector<sink_ptr> sinks_init_list;
typedef std::function<void(const std::string&)> err_handler;
typedef Shipyard::Logging::logging_view string_view_t;
typedef Shipyard::Logging::logging_buffer memory_buf_t;
typedef Shipyard::Atomic::atomic<int> level_t;
'''
    body=generic(body)
    make=''
    for n in range(9):
        params=','.join('class A'+str(i) for i in range(n));args=','.join('A%d&& a%d'%(i,i) for i in range(n));call=','.join('std::forward<A%d>(a%d)'%(i,i) for i in range(n))
        make+='template<class T'+(','+params if n else '')+'> std::unique_ptr<T> make_unique('+args+'){return std::unique_ptr<T>(new T('+call+'));}\n'
    make+='template<class T,class U> T conditional_static_cast(U value){return static_cast<T>(value);}\n'
    return declarations+body+make+'} }\n#include "common-inl.h"\n'
def generate(out):
    texts={'common.h':common()}
    t=read('common-inl.h');t=t.replace('memory_buf_t outbuf;','fmt::MemoryWriter outbuf;').replace('fmt::to_string(outbuf)','std::string(outbuf.data(),outbuf.size())')
    t=t.replace('level_string_views[] SPDLOG_LEVEL_NAMES','level_string_views[] = SPDLOG_LEVEL_NAMES').replace('short_level_names[] SPDLOG_SHORT_LEVEL_NAMES','short_level_names[] = SPDLOG_SHORT_LEVEL_NAMES')
    texts['common-inl.h']=generic(t)
    texts['fmt/fmt.h']='#pragma once\n#include "'+(BASE/'format_types.h').as_posix()+'"\n'
    t=read('details/os.h');t=t.replace('int pid() SPDLOG_NOEXCEPT','int pid()')
    texts['details/os.h']=generic(t)
    t=read('details/os-inl.h').replace('#include <spdlog/details/windows_include.h>','#include "'+(BASE/'native.h').as_posix()+'"\n#include <share.h>')
    t=t.replace('#include <cstdlib>','#include <cstdlib>\n#ifdef _XBOX\n#include "'+ENV_HEADER.as_posix()+'"\n#endif')
    t=t.replace('    char *buf = std::getenv(field);','#ifdef _XBOX\n    char *buf = ::'+ENV_GET+'(field);\n#else\n    char *buf = std::getenv(field);\n#endif')
    t=t.replace('::localtime_s(&tm, &time_tt);','::__time64_t wide_time=static_cast<::__time64_t>(time_tt);\n    ::_localtime64_s(&tm, &wide_time);')
    t=t.replace('::gmtime_s(&tm, &time_tt);','::__time64_t wide_time=static_cast<::__time64_t>(time_tt);\n    ::_gmtime64_s(&tm, &wide_time);')
    t=t.replace('#if _WIN32_WINNT < _WIN32_WINNT_WS08','#if defined(_XBOX) || _WIN32_WINNT < _WIN32_WINNT_WS08')
    t=t.replace('int pid() SPDLOG_NOEXCEPT','int pid()')
    t=t.replace('    return conditional_static_cast<int>(::GetCurrentProcessId());','#ifdef _XBOX\n    throw_spdlog_ex("Process IDs are unavailable on Xbox 360");\n#else\n    return conditional_static_cast<int>(::GetCurrentProcessId());\n#endif')
    t=t.replace('    return true;\n#else\n\n    static const bool result', '    return false; // native console has no terminal-color API\n#else\n\n    static const bool result')
    t=t.replace('    return ::_isatty(_fileno(file)) != 0;','    return false; // no native terminal attached')
    t=t.replace("subdir.length() == 2 && subdir[1] == ':'","!subdir.empty() && subdir[subdir.size()-1] == ':'")
    texts['details/os-inl.h']=generic(t)
    t=read('details/log_msg.h')
    t=t.replace('log_msg() = default;','log_msg() : level(level::off),thread_id(0),color_range_start(0),color_range_end(0) {}')
    t=t.replace('    log_msg(const log_msg &other) = default;','').replace('    log_msg &operator=(const log_msg &other) = default;','')
    for old,new in [('level{level::off}','level'),('thread_id{0}','thread_id'),('color_range_start{0}','color_range_start'),('color_range_end{0}','color_range_end')]:t=t.replace(old,new)
    texts['details/log_msg.h']=generic(t)
    t=read('details/log_msg-inl.h');t=t.replace('      source(loc),','      color_range_start(0), color_range_end(0), source(loc),')
    t=t.replace(': log_msg(os::now(), loc, a_logger_name, lvl, msg) {}',': logger_name(a_logger_name),level(lvl),time(os::now()),thread_id(os::thread_id()),color_range_start(0),color_range_end(0),source(loc),payload(msg) {}')
    t=t.replace(': log_msg(os::now(), source_loc{}, a_logger_name, lvl, msg) {}',': logger_name(a_logger_name),level(lvl),time(os::now()),thread_id(os::thread_id()),color_range_start(0),color_range_end(0),source(),payload(msg) {}')
    texts['details/log_msg-inl.h']=generic(t)
    for name in ('details/log_msg_buffer.h','details/log_msg_buffer-inl.h','formatter.h'):
        t=read(name).replace('log_msg_buffer() = default;','log_msg_buffer() {}').replace('virtual ~formatter() = default;','virtual ~formatter() {}')
        t=t.replace('log_msg{orig_msg}','log_msg(orig_msg)').replace('log_msg{other}','log_msg(other)').replace('buffer{std::move(other.buffer)}','buffer(std::move(other.buffer))')
        texts[name]=generic(t)
    t=read('details/fmt_helper.h').replace('fmt::format_int','fmt::FormatInt')
    t=t.replace('fmt_lib::format_to(std::back_inserter(dest), SPDLOG_FMT_STRING("{:02}"), n);','fmt::MemoryWriter writer; writer.write("{:02}",n); dest.append(writer.data(),writer.data()+writer.size());')
    texts['details/fmt_helper.h']=generic(t)
    t=read('pattern_formatter.h')
    t=t.replace('padding_info() = default;','padding_info() : width_(0),side_(pad_side::left),truncate_(false),enabled_(false) {}')
    for old,new in [('width_ = 0','width_'),('side_ = pad_side::left','side_'),('truncate_ = false','truncate_'),('enabled_ = false','enabled_')]:t=t.replace(old,new)
    t=t.replace('flag_formatter() = default;','flag_formatter() {}').replace('virtual ~flag_formatter() = default;','virtual ~flag_formatter() {}')
    t=t.replace(' = delete;',';')
    start=t.index('    template <typename T, typename... Args>');end=t.index('    void set_pattern',start)
    overloads=''
    for n in range(9):
        params=','.join('class A'+str(i) for i in range(n));args=','.join('A%d&& a%d'%(i,i) for i in range(n));call=','.join('std::forward<A%d>(a%d)'%(i,i) for i in range(n))
        overloads+='template<class T'+(','+params if n else '')+'> pattern_formatter& add_flag(char flag'+(','+args if n else '')+'){custom_handlers_[flag]=details::make_unique<T>('+call+'); return *this;}\n'
    t=t[:start]+overloads+t[end:];texts['pattern_formatter.h']=generic(t)
    t=read('pattern_formatter-inl.h')
    t=re.sub(r'(static (?:const )?std::array<[^;]+?>\s+\w+)\s*\{',r'\1 = {',t)
    t=t.replace('dest_(dest) {','dest_(dest), spaces_("                                                                ",64) {',1)
    t=t.replace('string_view_t spaces_{"                                                                ", 64};','string_view_t spaces_;')
    t=t.replace('z_formatter() = default;','z_formatter() : last_update_(Shipyard::Logging::chrono::seconds(0)),offset_minutes_(0) {}')
    t=t.replace('explicit z_formatter(padding_info padinfo)\n        : flag_formatter(padinfo) {}','explicit z_formatter(padding_info padinfo)\n        : flag_formatter(padinfo),last_update_(Shipyard::Logging::chrono::seconds(0)),offset_minutes_(0) {}')
    t=t.replace('log_clock::time_point last_update_{std::chrono::seconds(0)};','log_clock::time_point last_update_;').replace('int offset_minutes_{0};','int offset_minutes_;')
    t=t.replace('aggregate_formatter() = default;','aggregate_formatter() {}').replace(' = delete;',';')
    t=t.replace('explicit full_formatter(padding_info padinfo)\n        : flag_formatter(padinfo) {}','explicit full_formatter(padding_info padinfo)\n        : flag_formatter(padinfo),cache_timestamp_(0) {}')
    t=t.replace('std::chrono::seconds cache_timestamp_{0};','std::chrono::seconds cache_timestamp_;')
    t=t.replace('return cloned;','return std::unique_ptr<formatter>(cloned.release());')
    t=re.sub(r'(string_view_t\s+\w+)\{([^\n]+)\};',r'\1(\2);',t)
    texts['pattern_formatter-inl.h']=generic(t)
    t=read('details/backtracer.h').replace('std::atomic<bool> enabled_{false};','std::atomic<bool> enabled_;').replace('backtracer() = default;','backtracer() : enabled_(false) {}')
    texts['details/backtracer.h']=generic(t)
    t=read('details/backtracer-inl.h');t=re.sub(r'(std::lock_guard<std::mutex>\s+\w+)\{([^}]+)\}',r'\1(\2)',t)
    texts['details/backtracer-inl.h']=generic(t)
    t=read('sinks/sink.h');t=t.replace('virtual ~sink() = default;','virtual ~sink() {}').replace('level_t level_{level::trace};','level_t level_;')
    t=t.replace('class SPDLOG_API sink {\npublic:','class SPDLOG_API sink {\npublic:\n    sink() : level_(level::trace) {}')
    texts['sinks/sink.h']=generic(t);texts['sinks/sink-inl.h']=generic(read('sinks/sink-inl.h'))
    t=read('logger.h')
    t=t.replace('sinks_() {}','sinks_(),level_(level::info),flush_level_(level::off),custom_err_handler_() {}')
    t=t.replace('sinks_(begin, end) {}','sinks_(begin, end),level_(level::info),flush_level_(level::off),custom_err_handler_() {}')
    t=t.replace(': logger(std::move(name), {std::move(single_sink)}) {}',': name_(std::move(name)),sinks_(1,single_sink),level_(level::info),flush_level_(level::off),custom_err_handler_() {}')
    t=t.replace(': logger(std::move(name), sinks.begin(), sinks.end()) {}',': name_(std::move(name)),sinks_(sinks.begin(),sinks.end()),level_(level::info),flush_level_(level::off),custom_err_handler_() {}')
    t=t.replace('virtual ~logger() = default;','virtual ~logger() {}')
    start=t.index('    template <typename... Args>');end=t.index('    void log(log_clock::time_point',start)
    public='''    void log(source_loc loc,level::level_enum lvl,fmt::CStringRef format,fmt::ArgList args){log_(loc,lvl,format,args);}
    FMT_VARIADIC(void,log,source_loc,level::level_enum,fmt::CStringRef)
    void log(level::level_enum lvl,fmt::CStringRef format,fmt::ArgList args){log_(source_loc(),lvl,format,args);}
    FMT_VARIADIC(void,log,level::level_enum,fmt::CStringRef)
    void log(source_loc loc,level::level_enum lvl,const char* msg){log_(loc,lvl,fmt::CStringRef(msg),fmt::ArgList());}
    void log(level::level_enum lvl,const char* msg){log_(source_loc(),lvl,fmt::CStringRef(msg),fmt::ArgList());}
'''
    t=t[:start]+public+t[end:]
    start=t.index('    template <typename... Args>');end=t.index('    // return true logging is enabled',start)
    api=''
    for name,lvl in [('trace','trace'),('debug','debug'),('info','info'),('warn','warn'),('error','err'),('critical','critical')]:
        api+='    void '+name+'(fmt::CStringRef format,fmt::ArgList args){log_(source_loc(),level::'+lvl+',format,args);}\n    FMT_VARIADIC(void,'+name+',fmt::CStringRef)\n'
    t=t[:start]+api+t[end:]
    for old,new in [('level_{level::info}','level_'),('flush_level_{level::off}','flush_level_'),('custom_err_handler_{nullptr}','custom_err_handler_')]:t=t.replace(old,new)
    start=t.index('    template <typename... Args>');end=t.index('    // log the given message',start)
    method=t[start:t.index('\n#ifdef SPDLOG_WCHAR_TO_UTF8_SUPPORT',start)]
    method=method.replace('    template <typename... Args>\n','').replace('string_view_t fmt, Args &&...args','fmt::CStringRef format,fmt::ArgList args')
    s=method.index('            memory_buf_t buf;');e=method.index('\n\n            details::log_msg',s)
    method=method[:s]+'            fmt::MemoryWriter buf;\n            buf.write(format,args);'+method[e:]
    t=t[:start]+method+'\n'+t[end:];texts['logger.h']=generic(large_format_overloads(t,mode=OVERLOAD_MODE))
    t=read('logger-inl.h');t=re.sub(r'(std::lock_guard<std::mutex>\s+\w+)\{([^}]+)\}',r'\1(\2)',t)
    texts['logger-inl.h']=generic(t)
    t=read('async_logger.h');texts['async_logger.h']=generic(t)
    t=read('async_logger-inl.h')
    start=t.index('    : async_logger(std::move(logger_name),');end=t.index('\n\n',start)
    t=t[:start]+'    : logger(std::move(logger_name),sinks_list.begin(),sinks_list.end()),thread_pool_(std::move(tp)),overflow_policy_(overflow_policy) {}'+t[end:]
    start=t.index('    : async_logger(\n');end=t.index('\n\n',start)
    t=t[:start]+'    : logger(std::move(logger_name),single_sink),thread_pool_(std::move(tp)),overflow_policy_(overflow_policy) {}'+t[end:]
    texts['async_logger-inl.h']=generic(t)
    t=read('details/thread_pool.h').replace('std::vector<std::thread>','std::vector<Shipyard::Logging::joining_thread>')
    t=t.replace('async_msg_type msg_type{async_msg_type::log};','async_msg_type msg_type;').replace('async_msg() = default;','async_msg() : msg_type(async_msg_type::log) {}').replace('~async_msg() = default;','~async_msg() {}')
    t=t.replace('~async_msg() : msg_type(async_msg_type::log) {}','~async_msg() {}')
    t=t.replace(' = delete;',';')
    t=t.replace('log_msg_buffer{m}','log_msg_buffer(m)').replace('msg_type{the_type}','msg_type(the_type)').replace('worker_ptr{std::move(worker)}','worker_ptr(std::move(worker))').replace('log_msg_buffer{}','log_msg_buffer()')
    t=t.replace(': async_msg{nullptr, the_type} {}',': log_msg_buffer(),msg_type(the_type),worker_ptr() {}')
    # Use current explicit MSVC move implementation on both hosts for differential tests.
    t=t.replace('#if defined(_MSC_VER) && _MSC_VER <= 1800','#if 1')
    texts['details/thread_pool.h']=generic(t)
    t=read('details/thread_pool-inl.h')
    t=t.replace('threads_.emplace_back([this, on_thread_start, on_thread_stop] {','threads_.push_back(Shipyard::Logging::LoggingMakeThread([this, on_thread_start, on_thread_stop] {').replace('            on_thread_stop();\n        });','            on_thread_stop();\n        }));')
    # A joining native worker must receive termination if a later creation fails.
    t=t.replace('    for (size_t i = 0; i < threads_n; i++) {','    threads_.reserve(threads_n);\n    try { for (size_t i = 0; i < threads_n; i++) {',1)
    marker='        }));\n    }\n}'
    t=exact(t,marker,'        }));\n    } } catch(...) {\n        for(size_t i=0;i<threads_.size();++i) post_async_msg_(async_msg(async_msg_type::terminate),async_overflow_policy::block);\n        for(size_t i=0;i<threads_.size();++i) threads_[i].join();\n        throw;\n    }\n}')
    start=t.index('    : q_(q_max_items) {');end=t.index('\n}\n',start)+2;body=t[start:end]
    t=t.replace(': thread_pool(q_max_items, threads_n, std::move(on_thread_start), [] {}) {}',body.replace('    : q_',': q_',1).replace('    if (threads_n','    std::function<void()> on_thread_stop=[] {};\n    if (threads_n',1))
    t=t.replace(': thread_pool(q_max_items, threads_n, [] {}, [] {}) {}',body.replace('    : q_',': q_',1).replace('    if (threads_n','    std::function<void()> on_thread_start=[] {},on_thread_stop=[] {};\n    if (threads_n',1))
    texts['details/thread_pool-inl.h']=generic(t)
    t=read('details/registry.h').replace('#include <spdlog/details/periodic_worker.h>','')
    start=t.index('    template <typename Rep, typename Period>');end=t.index('    void set_error_handler',start);t=t[:start]+t[end:]
    t=t.replace('    std::unique_ptr<periodic_worker> periodic_flusher_;','')
    for old,new in [('global_log_level_ = level::info','global_log_level_'),('flush_level_ = level::off','flush_level_'),('automatic_registration_ = true','automatic_registration_'),('backtrace_n_messages_ = 0','backtrace_n_messages_')]:t=t.replace(old,new)
    t=t.replace(' = delete;',';');texts['details/registry.h']=generic(t)
    t=read('details/registry-inl.h').replace('#include <spdlog/details/periodic_worker.h>','')
    t=t.replace('#include <spdlog/sinks/wincolor_sink.h>','#include <spdlog/sinks/stdout_sinks.h>').replace('#include <spdlog/sinks/ansicolor_sink.h>','#include <spdlog/sinks/stdout_sinks.h>')
    t=t.replace('sinks::wincolor_stdout_sink_mt','sinks::stdout_sink_mt').replace('sinks::ansicolor_stdout_sink_mt','sinks::stdout_sink_mt')
    t=t.replace(': formatter_(new pattern_formatter()) {',': formatter_(new pattern_formatter()),global_log_level_(level::info),flush_level_(level::off),automatic_registration_(true),backtrace_n_messages_(0) {')
    t=t.replace('registry::~registry() = default;','registry::~registry() {}')
    t=t.replace('    {\n        std::lock_guard<std::mutex> lock(flusher_mutex_);\n        periodic_flusher_.reset();\n    }\n','')
    texts['details/registry-inl.h']=generic(t)
    t=read('async.h');start=t.index('// async logger factory');end=t.index('// set global thread pool.',start);t=t[:start]+t[end:]
    texts['async.h']=generic(t)
    t=read('spdlog.h').replace('#include <spdlog/details/synchronous_factory.h>','').replace('using default_factory = synchronous_factory;','')
    start=t.index('template <typename Sink, typename... SinkArgs>');end=t.index('// Initialize and register a logger,',start);t=t[:start]+t[end:]
    start=t.index('template <typename Rep, typename Period>');end=t.index('// Set global error handler',start);t=t[:start]+t[end:]
    start=t.index('template <typename... Args>');end=t.index('}  // namespace spdlog',start)
    api='inline void log(source_loc source,level::level_enum lvl,fmt::CStringRef format,fmt::ArgList args){default_logger_raw()->log(source,lvl,format,args);}\nFMT_VARIADIC(void,log,source_loc,level::level_enum,fmt::CStringRef)\n'
    api+='inline void log(level::level_enum lvl,fmt::CStringRef format,fmt::ArgList args){default_logger_raw()->log(source_loc(),lvl,format,args);}\nFMT_VARIADIC(void,log,level::level_enum,fmt::CStringRef)\n'
    for name,lvl in [('trace','trace'),('debug','debug'),('info','info'),('warn','warn'),('error','err'),('critical','critical')]:api+='inline void '+name+'(fmt::CStringRef format,fmt::ArgList args){default_logger_raw()->log(source_loc(),level::'+lvl+',format,args);}\nFMT_VARIADIC(void,'+name+',fmt::CStringRef)\n'
    t=t[:start]+api+t[end:];t=t.replace('spdlog::source_loc{__FILE__, __LINE__, SPDLOG_FUNCTION}','spdlog::source_loc(__FILE__, __LINE__, SPDLOG_FUNCTION)').replace('spdlog::source_loc{}','spdlog::source_loc()')
    texts['spdlog.h']=generic(large_format_overloads(t,free_functions=True,mode=OVERLOAD_MODE));texts['spdlog-inl.h']=generic(read('spdlog-inl.h'));texts['version.h']=read('version.h')
    t=read('details/null_mutex.h').replace('null_atomic_int() = default;','null_atomic_int() : value(0) {}').replace('std::memory_order = std::memory_order_relaxed','')
    t=t.replace('int new_value, )','int new_value)');texts['details/null_mutex.h']=generic(t)
    t=read('sinks/base_sink.h').replace('~base_sink() override = default;','~base_sink() {}').replace(' = delete;',';');texts['sinks/base_sink.h']=generic(t)
    t=read('sinks/base_sink-inl.h').replace('formatter_{details::make_unique<spdlog::pattern_formatter>()}','formatter_(details::make_unique<spdlog::pattern_formatter>())').replace('formatter_{std::move(formatter)}','formatter_(std::move(formatter))');texts['sinks/base_sink-inl.h']=generic(t)
    t=read('details/file_helper.h').replace('file_helper() = default;','file_helper() : open_tries_(5),open_interval_(10),fd_(0) {}').replace(' = delete;',';')
    t=t.replace('open_tries_ = 5','open_tries_').replace('open_interval_ = 10','open_interval_').replace('fd_{nullptr}','fd_');texts['details/file_helper.h']=generic(t)
    t=read('details/file_helper-inl.h').replace(': event_handlers_(event_handlers) {}',': open_tries_(5),open_interval_(10),fd_(0),event_handlers_(event_handlers) {}');texts['details/file_helper-inl.h']=generic(t)
    t=read('sinks/rotating_file_sink.h').replace('#include <spdlog/details/synchronous_factory.h>','').replace('static constexpr size_t','static const size_t').replace('event_handlers = {}','event_handlers = file_event_handlers()')
    start=t.index('// factory functions');end=t.index('}  // namespace spdlog',start);t=t[:start]+t[end:];texts['sinks/rotating_file_sink.h']=generic(t)
    t=read('sinks/rotating_file_sink-inl.h').replace('file_helper_{event_handlers}','file_helper_(event_handlers)');texts['sinks/rotating_file_sink-inl.h']=generic(t)
    texts['details/console_globals.h']=generic(read('details/console_globals.h'))
    for name in ('sinks/stdout_sinks.h','sinks/stdout_sinks-inl.h'):
        t=read(name).replace('#include <spdlog/details/synchronous_factory.h>','')
        t=t.replace('#ifdef _WIN32','#if 0 // Xbox uses the actual CRT fwrite branch')
        t=t.replace('~stdout_sink_base() override = default;','~stdout_sink_base() {}').replace(' = delete;',';')
        start=t.index('// factory methods');end=t.index('}  // namespace spdlog',start);t=t[:start]+t[end:]
        texts[name]=generic(t)
    read('sinks/stdout_color_sinks.h') # Validate the source whose API is projected.
    texts['sinks/stdout_color_sinks.h']='''#pragma once
#include <spdlog/sinks/stdout_sinks.h>
namespace spdlog { namespace sinks {
typedef stdout_sink_mt stdout_color_sink_mt;
typedef stdout_sink_st stdout_color_sink_st;
typedef stderr_sink_mt stderr_color_sink_mt;
typedef stderr_sink_st stderr_color_sink_st;
} }
'''
    # XDK 21256 /O2 emits a fallthrough into its EH metadata after the final
    # backend_flush_ call in this function. The production logger worker faults
    # there (20260919-214006-345, guest PC 8244C414), before GPU work can run.
    # Keep the exact queue/sink/flush/error behavior; constrain only this
    # function's code generation and restore the surrounding compiler options.
    name='async_logger-inl.h'
    t=texts[name]
    begin='SPDLOG_INLINE void spdlog::async_logger::backend_sink_it_('
    end='SPDLOG_INLINE void spdlog::async_logger::backend_flush_('
    t=exact(t,begin,'#if defined(_XBOX)\n#pragma optimize("", off)\n#endif\n'+begin)
    t=exact(t,end,'#if defined(_XBOX)\n#pragma optimize("", on)\n#endif\n'+end)
    texts[name]=t
    for name,t in texts.items():
        t=t.replace('Shipyard::Logging::',NAMESPACE+'::')
        p=out/'spdlog'/name;p.parent.mkdir(parents=True,exist_ok=True)
        PRISM.mapped((UP/'spdlog'/name).as_posix(),t,p)
        p.write_text(p.read_text().replace(GUARD_SOURCE,GUARD_TARGET))
        texts[name]=t
    return texts
