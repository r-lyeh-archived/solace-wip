// solace - a console replacement. forked and rewritten from https://github.com/rxi/lovebird
// - rlyeh, zlib/libpng licensed

#pragma once

#include <iostream>
#include <vector>
#include <string>
#include <map>

namespace solace {
    // public logging api
    extern std::ostream &cout;

    //
    void set_highlights( const std::vector<std::string>& );

    // std::ostream capture; when you want to capture std::cout, std::cerr, and others.
    bool capture( std::ostream &out );
    bool release( std::ostream &out );

    // stdout, stderr capture; when you want to capture printf() and company.
    bool capture( int fd );
    bool release( int fd );

    // install html server with custom callbacks
    bool webinstall(
        // port to listen to
        int port = 8080,
        // script/expression evaluator (like LUA, angelscript, your own, or null for none)
        std::string (*eval)( const std::string &cmd ) = 0,
        // list of current symbols in keypath level tree (or null for none)
        // - if value is empty subnode type is assumed,
        // - else if value is \"quoted\" string type is assumed,
        // - else number type is assumed.
        std::map<std::string,std::string> (*get_property_list)( const std::string &keypath ) = 0
    );
    bool webopen();

    // get local webhome directory
    std::string webhome( const std::string &suffix = std::string() );
}
