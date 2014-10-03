// created by i-saint
// distributed under Creative Commons Attribution (CC BY) license.
// https://github.com/i-saint/WebDebugMenu

#ifndef wdmStatic
#define wdmStatic
#endif

#include "WebDebugMenu.h"
#include "solace.hpp"
#include <route66/route66.hpp>

#ifdef _WIN32
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "shell32.lib")
#include <windows.h>
#include <psapi.h>
#include <shellapi.h>
#   ifndef wdmDisableEnumMemberVariables
#   include <dbghelp.h>
#       pragma comment(lib, "dbghelp.lib")
#   endif // wdmDisableEnumMemberVariables
#endif // _WIN32

#include <stdio.h>
#include <string>
#include <map>
#include <vector>

extern const std::string html_10002;

using std::int8_t;
using std::uint8_t;

using std::int16_t;
using std::uint16_t;

using std::int32_t;
using std::uint32_t;

using std::int64_t;
using std::uint64_t;

namespace {
    // taken from https://github.com/r-lyeh/wire {
    std::vector< std::string > tokenize( const std::string &self, const std::string &delimiters ) {
        std::string map( 256, '\0' );
        for( std::string::const_iterator it = delimiters.begin(), end = delimiters.end(); it != end; ++it ) {
            unsigned char ch( *it );
            map[ ch ] = '\1';
        }
        std::vector< std::string > tokens(1);
        for( std::string::const_iterator it = self.begin(), end = self.end(); it != end; ++it ) {
            unsigned char ch( *it );
            /**/ if( !map.at(ch)          ) tokens.back().push_back( char(ch) );
            else if( tokens.back().size() ) tokens.push_back( std::string() );
        }
        while( tokens.size() && !tokens.back().size() ) tokens.pop_back();
        return tokens;
    }
    bool match( const char *pattern, const char *str ) {
        if( *pattern=='\0' ) return !*str;
        if( *pattern=='*' )  return match(pattern+1, str) || *str && match(pattern, str+1);
        if( *pattern=='?' )  return *str && (*str != '.') && match(pattern+1, str+1);
        return (*str == *pattern) && match(pattern+1, str+1);
    }
    bool matches( const std::string &self, const std::string &pattern ) {
        return match( pattern.c_str(), self.c_str() );
    }
    // }    
}

// std::string が dll を跨ぐと問題が起きる可能性があるため、
// std::string を保持して dll を跨がない wdmEventData と、const char* だけ保持して dll 跨ぐ wdmEvent に分ける
struct wdmEventData
{
    wdmID node;
    wdmString command;

    wdmEvent toEvent() const
    {
        wdmEvent tmp = {node, command.c_str()};
        return tmp;
    }
};

struct wdmJSONRequest
{
    bool done;
    bool canceled;
    wdmString *json;
    const wdmID *nodes;
    uint32_t num_nodes;
};

wdmConfig::wdmConfig()
    : port(10002)
    , max_queue(100)
    , max_threads(2)
    , json_reserve_size(1024*1024)
    , disabled(false)
{
}

bool wdmConfig::load(const char *path)
{
    if(FILE *f=fopen(path, "rb")) {
        char buf[256];
        while(fgets(buf, sizeof(buf), f)) {
            uint32_t t;
            if     (sscanf(buf, "port: %d", &t)==1) { port=t; }
            else if(sscanf(buf, "max_queue: %d", &t)==1) { max_queue=t; }
            else if(sscanf(buf, "max_threads: %d", &t)==1) { max_threads=t; }
            else if (sscanf(buf, "json_reserve_size: %d", &t) == 1) { json_reserve_size = t; }
            else if (sscanf(buf, "disable: %d", &t) == 1) { disabled = t!=0; }
        }
        fclose(f);
        return true;
    }
    return false;
}

class wdmSystem
{
public:
    typedef std::map<wdmID, wdmNode*> node_cont;
    typedef std::vector<wdmEventData> event_cont;
    typedef std::vector<wdmJSONRequest*> json_cont;

    static void         createInstance();
    static void         releaseInstance();
    static wdmSystem*   getInstance();

    wdmSystem();
    ~wdmSystem();
    wdmID            generateID();
    wdmNode*         getRootNode() const;
    const wdmConfig* getConfig() const;
    void             registerNode(wdmNode *node);
    void             unregisterNode(wdmNode *node);
    void             addEvent(const wdmEventData &e);
    void             flushEvent();

    void             requestJSON(wdmJSONRequest &request);
    void             createJSON(wdmString &out, const wdmID *nodes, uint32_t num_nodes);
    void             clearRequests();

    bool             getEndFlag() const { return m_end_flag; }

private:
    static wdmSystem *s_inst;
    node_cont m_nodes;
    event_cont m_events;
    json_cont m_jsons;
    wdmNode *m_root;
    bool m_end_flag;
    uint32_t /*Poco::AtomicCounter*/ m_idgen;
    std::mutex m_mutex;

    wdmConfig m_conf;
    struct httpserver {
        bool Start();
        bool Stop();
    };
    httpserver *m_server;
};




// TODO!

#include <fstream>
#include <sstream>

namespace {

#ifdef _WIN32
/*
Mac OS X: _NSGetExecutablePath() (man 3 dyld)
Linux: readlink /proc/self/exe
Solaris: getexecname()
FreeBSD: sysctl CTL_KERN KERN_PROC KERN_PROC_PATHNAME -1
FreeBSD if it has procfs: readlink /proc/curproc/file (FreeBSD doesn't have procfs by default)
NetBSD: readlink /proc/curproc/exe
DragonFly BSD: readlink /proc/curproc/file
Windows: GetModuleFileName() with hModule = NULL
*/

    size_t GetModulePath(char *out_path, size_t len) {
        HMODULE mod = 0;
        ::GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, (LPCSTR)&GetModulePath, &mod);
        DWORD size = ::GetModuleFileNameA(mod, out_path, (DWORD)len);
        return size;
    }

    bool GetModuleDirectory(char *out_path, size_t len) {
        size_t size = GetModulePath(out_path, len);
        while(size>0) {
            if(out_path[size]=='\\') {
                out_path[size+1] = '\0';
                return true;
            }
            --size;
        }
        return false;
    }

    std::string GetCurrentModuleDirectory() {
        static char s_path[MAX_PATH] = {0};
        if(s_path[0]=='\0') {
            GetModuleDirectory(s_path, MAX_PATH);
        }
        return s_path;
    }
#else
    std::string GetCurrentModuleDirectory() {
        return "./";
    }

#endif

int wdmFileRequestHandler(route66::request &request, std::ostream &headers, std::ostream &contents) {
    std::ifstream ifs( request.uri.c_str(), std::ios::binary );
    if( ifs.good() ) {
        headers << route66::mime(request.uri);
        contents << ifs.rdbuf();
        return 200;
    }
    return 404;
}
}


template<class F>
void EachInputValue(route66::request &request, F &f) {
    size_t size = request.options.size();
    if( !size || size >1024*4 ) {
        return;
    }
    const std::string &content = request.arguments["command"];

#if 0
    std::regex reg("(\\d+)->([^;]+)");
    std::cmatch m;
    size_t pos = 0;
    for(;;) {
        if(std::regex_search(content.c_str()+pos, m, reg)) {
            f(m[1].str().c_str(), m[2].str().c_str());
            pos += m.position()+m.length();
        }
        else {
            break;
        }
    }
#else
    //std::cout << "icontent:" << content << std::endl;
    std::vector<std::string> tokens = tokenize(content, ">; \t\r\n\f\v");
    for( std::vector<std::string>::const_iterator it = tokens.begin(), end = tokens.end(); it != end; ++it ) {
        int m;
        const char * const number = it->c_str();
        const char * const text = (++it)->c_str();
        //std::cout << number << " >> " << text << std::endl;
        if( sscanf(number, "%d", &m) == 1 ) {
            f( number, text );
        } else {
            break;
        }
    }    
#endif
}

template<class F>
void EachNodeValue(route66::request &request, F &f) {
    size_t size = request.options.size();
    if( !size || size >1024*4 ) {
        return;
    }
    const std::string &content = request.arguments["nodes"];

#if 0
    std::regex reg("(\\d+)");
    std::cmatch m;
    size_t pos = 0;
    for(;;) {
        if(std::regex_search(content.c_str()+pos, m, reg)) {
            f(m[1].str().c_str());
            pos += m.position()+m.length();
        }
        else {
            break;
        }
    }
#else
    //std::cout << "ncontent:" << content << std::endl;
    std::vector<std::string> tokens = tokenize(content, " \t\r\n\f\v");
    for( std::vector<std::string>::const_iterator it = tokens.begin(), end = tokens.end(); it != end; ++it ) {
        int m;
        const char * const number = it->c_str();
        if( sscanf(number, "%d", &m) == 1 ) {
            f( number );
        } else {
            break;
        }
    }
#endif
}




wdmSystem* wdmSystem::s_inst = 0;

void wdmSystem::createInstance()
{
    if(s_inst==NULL) {
        new wdmSystem();
    }
}

void wdmSystem::releaseInstance()
{
    delete s_inst;
    s_inst = NULL;
}

wdmSystem* wdmSystem::getInstance()
{
    return s_inst;
}

int GET_root_old( route66::request &req, std::ostream &headers, std::ostream &contents ) {
    if(wdmSystem::getInstance()->getEndFlag()) { return 0; }

    req.uri = solace::webhome("10002/") + "/index.html";
    return wdmFileRequestHandler( req, headers, contents );
}

int GET_root( route66::request &req, std::ostream &headers, std::ostream &contents ) {
    if(wdmSystem::getInstance()->getEndFlag()) { return 0; }

    std::stringstream html;
    std::string path = solace::webhome("10002/") + "/index.html";

    {
        std::ifstream ifs(path.c_str(), std::ios::binary);
        if( ifs.good() ) {
            headers << route66::mime(".html");
            contents << ifs.rdbuf();
            return 200;
        }
    }
    {
        std::ofstream ofs(path.c_str(), std::ios::binary);
        if( ofs.good() ) {
            ofs << html_10002;
        }

        headers << route66::mime(".html");
        contents << html_10002;
        return 200;
    }
}

int GET_command( route66::request &req, std::ostream &headers, std::ostream &contents ) {
    if(wdmSystem::getInstance()->getEndFlag()) { return 0; }

    struct lambda {
        void operator()(const char *id, const char *command) const {
            wdmEventData tmp = {std::atoi(id), command};
            wdmSystem::getInstance()->addEvent(tmp);
        }
    } _;

    EachInputValue(req, _);

    headers << route66::mime(".text");
    contents << "ok";

    return 200;
}


int GET_data( route66::request &req, std::ostream &headers, std::ostream &contents ) {
    if(wdmSystem::getInstance()->getEndFlag()) { return 0; }

    struct lambda {
        std::vector<wdmID> nodes;
        void operator()(const char *id) {
            nodes.push_back(std::atoi(id));
        }
    } _;

    _.nodes.push_back(_wdmGetRootNode()->getID());
    EachNodeValue(req, _);

    wdmString json;
    wdmSystem::getInstance()->createJSON( json, _.nodes.empty() ? NULL : &_.nodes[0], (uint32_t)_.nodes.size() );
    //if(request.canceled) { json="[]"; }

    headers << route66::mime(".json");
    contents << json;

    return 200;
}

int GET_any( route66::request &req, std::ostream &headers, std::ostream &contents ) {
    if(wdmSystem::getInstance()->getEndFlag()) { return 0; }

    req.uri = solace::webhome("10002/") + req.uri;
    return wdmFileRequestHandler( req, headers, contents );
}

wdmSystem::wdmSystem()
    : m_end_flag(false)
    , m_root(NULL)
    , m_server(NULL)
    , m_idgen(0)
{
    s_inst = this;
    m_conf.load((GetCurrentModuleDirectory()+"wdmConfig.txt").c_str());
    m_conf.print();
    m_root = new wdmNodeBase();

#ifndef wdmDisableEnumMemberVariables
    ::SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_DEBUG);
    ::SymInitialize(::GetCurrentProcess(), NULL, TRUE);
#endif // wdmDisableEnumMemberVariables


	if (!m_server && !m_conf.disabled) {
        int MaxQueued(m_conf.max_queue);
        int MaxThreads(m_conf.max_threads);
        //std::chrono::seconds ThreadIdleTime(3);

        route66::create( m_conf.port, "GET /", GET_root);

        route66::create( m_conf.port, "* /command", GET_command);

        route66::create( m_conf.port, "* /data", GET_data);

        route66::create( m_conf.port, "GET *", GET_any);
    }
}

wdmSystem::~wdmSystem()
{
    m_end_flag = true;

    if(m_server) {
        //m_server->stop();
        while(0) { //m_server->currentThreads()>0) {
            clearRequests();
#ifdef _WIN32
            Sleep(5);
#else
            usleep(5000);
#endif
        }
        delete m_server;
        m_server = NULL;
    }
    if (m_root) {
        m_root->release();
        m_root = NULL;
        m_nodes.clear();
    }
}

wdmID wdmSystem::generateID()
{
    return ++m_idgen /* = 0 */;
}

wdmNode* wdmSystem::getRootNode() const
{
    return m_root;
}

const wdmConfig* wdmSystem::getConfig() const
{
    return &m_conf;
}

void wdmSystem::registerNode( wdmNode *node )
{
    if(node!=NULL) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_nodes[node->getID()] = node;
    }
}

void wdmSystem::unregisterNode( wdmNode *node )
{
    if(node!=NULL) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_nodes.erase(node->getID());
    }
}

void wdmSystem::addEvent( const wdmEventData &e )
{
    if(m_end_flag) { return; }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_events.push_back(e);
}

void wdmSystem::flushEvent()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    for(event_cont::iterator ei=m_events.begin(); ei!=m_events.end(); ++ei) {
        const wdmEventData e = *ei;
        node_cont::iterator ni = m_nodes.find(e.node);
        if(ni!=m_nodes.end()) {
            ni->second->handleEvent(e.toEvent());
        }
    }
    m_events.clear();

    for(json_cont::iterator ji=m_jsons.begin(); ji!=m_jsons.end(); ++ji) {
        wdmJSONRequest &req = **ji;
        createJSON(*req.json, req.nodes, req.num_nodes);
        req.done = true;
    }
    m_jsons.clear();
}

void wdmSystem::requestJSON(wdmJSONRequest &request)
{
    if(m_end_flag) { request.done=request.canceled=true; return; }
    std::lock_guard<std::mutex> lock(m_mutex);
    m_jsons.push_back(&request);
}

void wdmSystem::createJSON(wdmString &out, const wdmID *nodes, uint32_t num_nodes)
{
    out.resize(m_conf.json_reserve_size);
    size_t s = 0;
    for(;;) {
        s += wdmSNPrintf(&out[0]+s, out.size()-s, "[");
        {
            bool  first = true;
            for(size_t i=0; i<num_nodes; ++i) {
                node_cont::iterator p = m_nodes.find(nodes[i]);
                if(p!=m_nodes.end()) {
                    if(!first) { s += wdmSNPrintf(&out[0]+s, out.size()-s, ", "); }
                    s += p->second->jsonize(&out[0]+s, out.size()-s, 1);
                    first = false;
                }
            }
        }
        s += wdmSNPrintf(&out[0]+s, out.size()-s, "]");

        if(s==out.size()) {
            out.resize(out.size()*2);
        }
        else {
            break;
        }
    }
    out.resize(s);
}

void wdmSystem::clearRequests()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    m_events.clear();

    for(json_cont::iterator ji=m_jsons.begin(); ji!=m_jsons.end(); ++ji) {
        wdmJSONRequest &req = **ji;
        req.done = req.canceled = true;
    }
    m_jsons.clear();
}



wdmCLinkage wdmAPI void wdmInitialize() { wdmSystem::createInstance(); }
wdmCLinkage wdmAPI void wdmFinalize()   { wdmSystem::releaseInstance(); }

wdmCLinkage wdmAPI void wdmFlush()
{
    if(wdmSystem *sys = wdmSystem::getInstance()) {
        sys->flushEvent();
    }
}

wdmCLinkage wdmAPI void wdmOpenBrowser()
{
    if(const wdmConfig *conf = wdmGetConfig()) {
#ifdef _WIN32
        char url[256];
        sprintf(url, "http://localhost:%d", conf->port);
        ::ShellExecuteA(NULL, "open", url, "", "", SW_SHOWDEFAULT);
#endif // _WIN32
    }
}

wdmCLinkage wdmAPI const wdmConfig* wdmGetConfig()
{
    wdmSystem *sys = wdmSystem::getInstance();
    return sys ? sys->getConfig() : 0;
}

wdmCLinkage wdmAPI wdmID    _wdmGenerateID()                    { return wdmSystem::getInstance()->generateID(); }
wdmCLinkage wdmAPI wdmNode* _wdmGetRootNode()                   { return wdmSystem::getInstance()->getRootNode(); }
wdmCLinkage wdmAPI void     _wdmRegisterNode(wdmNode *node)     { wdmSystem::getInstance()->registerNode(node); }
wdmCLinkage wdmAPI void     _wdmUnregisterNode(wdmNode *node)   { wdmSystem::getInstance()->unregisterNode(node); }


#ifndef $QUOTE
#define $QUOTE(...) std::string(#__VA_ARGS__)
#endif

const std::string html_10002 = $QUOTE(
<!--
  -- created by i-saint
  -- distributed under Creative Commons Attribution (CC BY) license.
  -- /* https://github.com/i-saint/WebDebugMenu */
  -->
<!DOCTYPE html>
<meta charset="utf-8">
<html>
<head>
<title>WebDebugMenu</title>

 <style type="text/css">
 .data_property {vertical-align:top; display:inline-block; }
 .data_children {margin:2px 2px 2px 20px;}
 .data_type {margin:0px 5px 0px 5px;}
 .data_inputs {display:inline-block;}
 .data_control {display:inline-block;}
 .numeric_slider {width:100px; height:8px;}
 input.numeric {width:95px;}
 </style>

<!-- script type="text/javascript" src="http://ajax.googleapis.com/ajax/libs/prototype/1.7.2.0/prototype.js"></script -->

<link href="http://code.jquery.com/ui/1.11.1/themes/smoothness/jquery-ui.min.css" rel="stylesheet" type="text/css" />
<script src="http://code.jquery.com/jquery-1.11.0.min.js"></script>
<script src="http://code.jquery.com/ui/1.11.1/jquery-ui.min.js"></script>
<script src="http://jqueryui.com/resources/demos/external/jquery-mousewheel/jquery.mousewheel.js"></script>

<script type="text/javascript">

var node_data = {};
var node_loading = false;
var node_timer;
var opened_nodes = [];

var create_input_commons = function(s) {
    s.makeReadOnly = function() { $(this).prop('disabled',true); };
    s.handleChange = function() { this.parentNode.handleChange(this); };
};

var create_bool_input = function() {
    var i = $("<input>").attr({ class: "bool", type: "checkbox", onchange: "this.parentNode.handleChange()" });
    var s = $("<span>");
    s.append(i);
    s = s[0];
    s.inputNode = i;
    s.setValue = function(v) { if(!this.inputNode[0].lockUpdate) this.inputNode.prop('checked',v.value!=0); };
    s.getValue = function() { return this.inputNode.prop('checked') ? 1 : 0; };
    create_input_commons(s);
    return s;
};

var create_string_input = function(type) {
    var i = $("<input>").attr({
        class: type, 
        type: "text", 
        size: 16, 
        onfocus: "this.lockUpdate=true", 
        onblur: "this.lockUpdate=false", 
        onchange: "this.parentNode.handleChange()"
    });
    var s = $("<span>");
    s.append(i);
    s = s[0];
    s.inputNode = i;
    s.setValue = function(v) { if(!this.inputNode[0].lockUpdate) this.inputNode.val(v.value); };
    s.getValue = function() { return "\""+this.inputNode.val()+"\""; };
    s.setRange = function(r) {
    };
    create_input_commons(s);
    return s;
};

var create_integer_input = function(type) {
    var div = $("<div>").attr({
        class: "data_types",
    });

    var i,j;
    var inp = $("<input>").attr( { 
        class: "numeric "+type, 
        onfocus: "this.lockUpdate=true", 
        onblur: "this.parentNode.handleChange();this.lockUpdate=false", 
        onchange: "this.parentNode.handleChange();"
    });
    inp.lockUpdate = false;
    inp.refresh = function(value) {
        if( j.slider ('option','value') != value ) j.slider ('option','value',value);
        if( inp.val() != value ) inp.val(value);
        if( i.spinner('option','value') != value ) {
            i.spinner('option','value',value);
            return true;
        };
        return false;
    };
    inp.commit = function() {
        var value = inp.val();
        var r = [i.spinner('option','min'), i.spinner('option','max')];
        if( value < r[0] ) value = r[0];
        if( value > r[1] ) value = r[1];
        inp.refresh(value);

        inp[0].parentNode.handleChange();
        inp.lockUpdate = false;
    };

    i = inp.css({width: "50px", display:"inline-block"}).spinner({
        numberFormat: "n",
        step: 1, min: 0, max: 100,
        start: function(event,ui) {
            inp.lockUpdate = true;
        },
        spin: function(event,ui) {
            inp.refresh(ui.value);
        },
        stop: function(event,ui) {
            inp.commit();
        }
    });

    j = $("<div>").css({width: "100px", display: "inline-block"}).slider({
        step: 1, min: 0, max: 100,
        start: function(event,ui) {
            inp.lockUpdate = true;
        },
        slide: function(event,ui) {
            inp.refresh(ui.value);
        },
        stop: function(event,ui) {
            inp.commit();
        }
    });

    div.append(i);
    div.append(j);
    var s = div[0];

    s.setValue = function(v) { if(!inp.lockUpdate) { if(inp.refresh(v.value)) inp.commit(); } };
    s.getValue = function() { return inp.val(); };
    s.onSlider = function() { inp.lockUpdate = true; };
    s.setRange = function(r) {
        i.spinner("option", "min", r[0]);
        i.spinner("option", "max", r[1]);
        i.spinner("option", "value", inp.val());

        j.slider("option", "min", r[0]);
        j.slider("option", "max", r[1]);
        j.slider("option", "value", inp.val());
    };

    create_input_commons(s);

    s.makeReadOnly = function() { 
        i.spinner('option', 'disabled',true); 
        j.slider('option','disabled',true);
    };

    return s;
};

var create_float_input = function(type) {
    var i = $("<input>").attr( {
        class: "numeric "+type, 
        type: "text", 
        size: 8, 
        onfocus: "this.lockUpdate=true", 
        onblur: "this.lockUpdate=false", 
        onchange: "this.parentNode.handleChange()"
    });

    var s = $("<div>").attr({class: "data_control"});
    s.append(i);
    s = s[0];
    s.inputNode = i;
    s.setValue = function(v) { if(!this.inputNode[0].lockUpdate) this.inputNode.val(v.value); };
    s.getValue = function() { return this.inputNode.val(); };
    s.onSlider = function() {
        var p = this.slider.val()*0.01;
        var r = this.dataRange;
        var v = r[0]+(r[1]-r[0])*p;
        this.inputNode.val(v.toString());
        this.handleChange();
    };
    s.setRange = function(r) {
        this.dataRange = r;
        this.slider = $("<input>").attr({
            class : "numeric_slider", 
            type : "range", 
            onchange : "this.parentNode.onSlider()", 
            onmousedown : "this.parentNode.inputNode.lockUpdate=true", 
            onmouseup : "this.parentNode.inputNode.lockUpdate=false" 
        });
        this.slider.insertBefore(this.inputNode);
        $("<br>").insertBefore(this.inputNode);
    };
    create_input_commons(s);
    return s;
};



var DataInputCreators = {
    "int8":  function() { return create_integer_input("int8") },
    "int16": function() { return create_integer_input("int16") },
    "int32": function() { return create_integer_input("int32") },
    "int64": function() { return create_integer_input("int64") },
    "uint8": function() { return create_integer_input("uint8") },
    "uint16":function() { return create_integer_input("uint16") },
    "uint32":function() { return create_integer_input("uint32") },
    "uint64":function() { return create_integer_input("uint64") },
    "bool": create_bool_input,
    "float32": function() { return create_float_input("float32") },
    "float64": function() { return create_float_input("float64") },
    "char":   function() { return create_string_input("char") },
    "char*":  function() { return create_string_input("char*") },
    "wchar":  function() { return create_string_input("wchar") },
    "wchar*": function() { return create_string_input("wchar*") },
    "string": function() { return create_string_input("string") },
    "wstring":function() { return create_string_input("wstring") },
};

var create_control_common = function(node)
{
    node.setValue = function(v) { this.dataChildren.setValue(v); };
    node.getValue = function() { return this.dataChildren.getValue(); };
    node.setRange = function(r) { this.dataChildren.setRange(r); };
    node.makeReadOnly = function() { this.dataChildren.makeReadOnly(); };
};

var create_scalar_control = function(type, data)
{
    var t = DataInputCreators[type]();
    var d = $("<span>").attr({class: type});
    d.append($(t));
    d = d[0];
    d.dataChildren = t;
    d.handleChange = function(n) {
        postCommand(data.id.toString()+">>set(" + this.getValue() + ")");
    };
    create_control_common(d);
    return d;
};

var create_string_control = function(type, data)
{
    var t = DataInputCreators[type]();
    var d = $("<span>").attr({class: type});
    t[0].dataControl = d;
    d.append($(t));
    d = d[0];
    d.dataChildren = t;
    d.handleChange = function(n) {
        postCommand(data.id.toString()+">>set(" + this.getValue() + ")");
    };
    create_control_common(d);
    return d;
};
)+$QUOTE(
var create_array1_control = function(type, num_elements, data)
{
    var d = document.createElement("div");
    d.setAttribute("class", "data_control "+type+"x"+num_elements);

    var dataChildren = [];
    for(var i=0; i<num_elements; ++i) {
        var t = DataInputCreators[type]();
        t.dataControl = d;
        t.arrayIndex = i;
        $(d).append(t);
        dataChildren.push(t);
    };
    d.dataChildren = dataChildren;

    d.setValue = function(v) {
        for(var i=0; i<this.dataChildren.length; ++i) {
            this.dataChildren[i].setValue(v.value[i]);
        }
    };
    d.getValue = function() {
        var r = "[";
        for(var i=0; i<this.dataChildren.length; ++i) {
            r += this.dataChildren[i].getValue();
            if(i+1!=this.dataChildren.length) { r+=","; }
        }
        r += "]";
        return r;
    };
    d.setRange = function(r) {
        for(var i=0; i<this.dataChildren.length; ++i) {
            this.dataChildren[i].setRange(r);
        }
    };
    d.makeReadOnly = function() {
        for(var i=0; i<this.dataChildren.length; ++i) {
            this.dataChildren[i].makeReadOnly();
        }
    };
    d.handleChange = function(n) {
        var value = this.dataChildren[n.arrayIndex].getValue();
        postCommand(data.id.toString()+">>at(" + n.arrayIndex + "," + value + ")");
    };

    return d;
};

var create_vector_control = function(type, num_elements, data)
{
    var d = create_array1_control(type, num_elements, data);
    d.handleChange = function() {
        postCommand(data.id.toString()+">>set(" + this.getValue() + ")");
    };
    return d;
};

var create_array2_control = function(type, dy, dx, data)
{
    var d = document.createElement("div");
    d.setAttribute("class", "data_control "+type+"["+dy+"]["+dx+"]");

    d.dataChildren = [];
    for(var i=0; i<dy; ++i) {
        var t = create_array1_control(type, dx, data);
        t.arrayIndex = i;
        t.dataControl = d;
        t.handleChange = function(n) { this.parentNode.handleChange(this); };
        d.appendChild(t);
        d.dataChildren.push(t);
        d.appendChild(document.createElement("br"));
    };
    d.setValue = function(v) {
        for(var i=0; i<this.dataChildren.length; ++i) {
            this.dataChildren[i].setValue(v[i]);
        }
    };
    d.getValue = function() {
        var r = "[";
        for(var i=0; i<this.dataChildren.length; ++i) {
            r += this.dataChildren[i].getValue();
            if(i+1!=this.dataChildren.length) { r+=","; }
        };
        r += "]";
        return r;
    };
    d.setRange = function(r) {
        for(var i=0; i<this.dataChildren.length; ++i) {
            this.dataChildren[i].setRange(r);
        };
    };
    d.makeReadOnly = function() {
        for(var i=0; i<this.dataChildren.length; ++i) {
            this.dataChildren[i].makeReadOnly();
        };
    };
    d.handleChange = function(n) {
        var value = this.dataChildren[n.arrayIndex].getValue();
        postCommand(data.id.toString()+">>at(" + n.arrayIndex + "," + value + ")");
    };

    return d;
};

var DataControlCreators = {
    "int8":     function(data) { return data.length ? create_array1_control("int8",   data.length, data) : create_scalar_control("int8",   data); },
    "int16":    function(data) { return data.length ? create_array1_control("int16",  data.length, data) : create_scalar_control("int16",  data); },
    "int32":    function(data) { return data.length ? create_array1_control("int32",  data.length, data) : create_scalar_control("int32",  data); },
    "int64":    function(data) { return data.length ? create_array1_control("int64",  data.length, data) : create_scalar_control("int64",  data); },
    "uint8":    function(data) { return data.length ? create_array1_control("uint8",  data.length, data) : create_scalar_control("uint8",  data); },
    "uint16":   function(data) { return data.length ? create_array1_control("uint16", data.length, data) : create_scalar_control("uint16", data); },
    "uint32":   function(data) { return data.length ? create_array1_control("uint32", data.length, data) : create_scalar_control("uint32", data); },
    "uint64":   function(data) { return data.length ? create_array1_control("uint64", data.length, data) : create_scalar_control("uint64", data); },
    "bool":     function(data) { return data.length ? create_array1_control("bool",   data.length, data) : create_scalar_control("bool",   data); },
    "float32":  function(data) { return data.length ? create_array1_control("float32",data.length, data) : create_scalar_control("float32",data); },
    "float64":  function(data) { return data.length ? create_array1_control("float32",data.length, data) : create_scalar_control("float32",data); },

    "char" :    function(data) { return create_string_control("char", data); },
    "char*" :   function(data) { return create_string_control("char*", data); },
    "wchar" :   function(data) { return create_string_control("wchar", data); },
    "wchar*" :  function(data) { return create_string_control("wchar*", data); },
    "string" :  function(data) { return create_string_control("string", data); },
    "wstring" : function(data) { return create_string_control("wstring", data); },

    "int32x2":  function(data) { return data.length ? create_array2_control("int32", data.length, 2, data) : create_vector_control("int32", 2, data); },
    "int32x3":  function(data) { return data.length ? create_array2_control("int32", data.length, 3, data) : create_vector_control("int32", 3, data); },
    "int32x4":  function(data) { return data.length ? create_array2_control("int32", data.length, 4, data) : create_vector_control("int32", 4, data); },
    "float32x2":function(data) { return data.length ? create_array2_control("float32", data.length, 2, data) : create_vector_control("float32", 2, data); },
    "float32x3":function(data) { return data.length ? create_array2_control("float32", data.length, 3, data) : create_vector_control("float32", 3, data); },
    "float32x4":function(data) { return data.length ? create_array2_control("float32", data.length, 4, data) : create_vector_control("float32", 4, data); },

    "float32x2x2": function(data) { return create_array2_control("float32", 2, 2, data); },
    "float32x3x3": function(data) { return create_array2_control("float32", 3, 3, data); },
    "float32x4x3": function(data) { return create_array2_control("float32", 4, 3, data); },
    "float32x4x4": function(data) { return create_array2_control("float32", 4, 4, data); },
};

function getOrCreateNode(parent, data)
{
    var node = $("#node"+data.id)[0];

    if(node==null) {
        node = document.createElement("div");
        node.nodeID = data.id;
        node.setAttribute("class", "data_cell");
        node.setAttribute("id", "node"+data.id);
        node.isOpened = function(){ return false; };

        var prop = document.createElement("div");
        prop.setAttribute("class", "data_property");
        node.appendChild(prop);
        if(data.name!="" && data.hasChildren) {
            var toggle = document.createElement("input");
            toggle.setAttribute("type", "checkbox");
            toggle.setAttribute("onchange", "setVisibility("+data.id+")");
            toggle.appendChild(document.createTextNode("+"));
            prop.appendChild(toggle);
            node.toggleNode = toggle;
            node.isOpened = function(){ return $(this.toggleNode).prop('checked'); };
        };

        var name = document.createElement("span");
        name.setAttribute("class", "data_name");
        name.appendChild(document.createTextNode(data.name));
        prop.appendChild(name);

        if(data.type!=null) {
            var span = document.createElement("span");
            span.setAttribute("class", "data_type");
            var typename = data.type;
            if(data.length!=null) { typename+="["+data.length+"]"; };
            span.appendChild(document.createTextNode("("+typename+")"));
            prop.appendChild(span);

            var control = DataControlCreators[data.type](data);
            if(data.range) { control.setRange(data.range); };
            if(data.readonly) { control.makeReadOnly(); };
            $(node).append(control);
            node.dataNode = control;
        };
        if(data.callable) {
            node.argNodes = [];
            for(var i=0; i<data.argTypes.length; ++i) {
                var argtype = data.argTypes[i];
                var span = document.createElement("span");
                span.setAttribute("class", "data_type");
                span.appendChild(document.createTextNode("arg"+(i+1)+"("+argtype+")"));
                prop.appendChild(span);

                var control = DataControlCreators[argtype](data);
                control.argIndex = i;
                control.handleChange = function(n) {
                    postCommand(data.id.toString()+">>arg(" + this.argIndex + "," + this.getValue() + ")");
                };
                $(prop).append(control);
                node.argNodes.push(control);
            };
            var button = document.createElement("input");
            button.setAttribute("type", "button");
            button.setAttribute("onclick", "handleCall("+data.id+")");
            button.setAttribute("value", "call");
            prop.appendChild(button);
            node.callButton = button;
        };

        children = $('<div>').attr({class: "data_children"});
        node.appendChild(children[0]);
        node.dataChildren = children[0];
        $(node.dataChildren).css({display: data.name=="" ? "" : "none"});
        parent.appendChild(node);
    };
    if(node.dataNode && data) {
        node.dataNode.setValue(data);
    };
    return node;
};

function updateDataGrid(parent, data, depth)
{
    var node = getOrCreateNode(parent, data);
    node.updated = true;
    parent.parentNode.updated = true;
    if(data.children) {
        for(var i=0; i<data.children.length; ++i) {
            updateDataGrid(node.dataChildren, data.children[i], depth+1);
        }
    }
};

function gatherOpenedNodes(node)
{
    if(node==null) { return; }
    if(node.isOpened()) { opened_nodes.push(node.nodeID); }
    if(node.dataChildren) {
        var children = node.dataChildren.children;
        for(var i=0; i<children.length; ++i) {
            gatherOpenedNodes(children[i]);
        }
    }
};

function beforeUpdateData(node)
{
    if(node==null) { return; }
    node.updated = false;
    var children = node.dataChildren.children;
    if(children) {
        for(var i=0; i<children.length; ++i) {
            beforeUpdateData(children[i]);
        }
    }
};

function afterUpdateData(node)
{
    if(node==null) { return false; }

    var updated = node.updated;
    var children = node.dataChildren.children;
    if(children) {
        for(var i=0; i<children.length; ++i) {
            var r = afterUpdateData(children[i]);
            updated = updated || r;
        }
    }

    if(!updated) {
        node.parentNode.removeChild(node);
    }
    return updated;
};

function updateNodeData()
{
    if(node_loading) { return; }

    opened_nodes.length = 0;
    gatherOpenedNodes($("#data_grid")[0].children[0]);

    node_loading = true;
    $.post( "/data", "nodes="+opened_nodes.join(",") )
        .always( function(data,status) {

            node_loading = false;
            if(status != "success") {
                /* update text */
                $("#status").html("disconnected");
            } else {
                /* clear on reconnection */
                if( $("#status").html() != "connected" ) $("#data_grid").html("");
                /* update text */
                $("#status").html("connected");   

                var root = $("#data_grid")[0];
                beforeUpdateData(root.children[0]);
                $.each(data, function(index, d) {
                    var node = $("#node"+d.id)[0];
                    node = node==null ? root : node.dataChildren;
                    updateDataGrid(node, d, 0);
                });
                afterUpdateData(root.children[0]);
            }
        } );

    return true;
};

function setVisibility(id)
{
    var node = $("#node"+id)[0];
    var children = $(node.dataChildren);
    if($(node.toggleNode).prop('checked')) {
        children.css({display: ""});
        updateNodeData();
    }
    else {
        children.css({display: "none"});
    }
};

function handleCall(id)
{
    postCommand(id.toString()+">>call()");
};

function postCommand(values) {
    $.post("/command", "command="+values);
};

function onLoad() {
    updateNodeData();
    clearInterval(node_timer);
    node_timer = setInterval(updateNodeData, 1000);
};

</script>
</head>

<body onload="onLoad()">
    <div id="status"></div>
    <div id="data_grid" ></div>
</body>

</html>
);
