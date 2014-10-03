// solace - a console replacement. forked and rewritten from https://github.com/rxi/lovebird
// - rlyeh, boost licensed

#pragma once
#include <iostream>
#include <vector>
#include <string>

namespace solace {
    // public logging api
    extern std::ostream &cout;

    //
    void set_highlights( const std::vector<std::string>& );

    // std::ostream capture; when you want to capture std::cout, std::cerr, and others.
    bool capture( std::ostream &out );
    bool release( std::ostream &out );

    // html viewer with a custom callback to a script/expression evaluator (like LUA, angelscript, your own, or null for none)
    bool webinstall( int port = 8080, std::string (*eval)( const std::string &cmd ) = 0 );
    bool webopen();

    // get local webhome directory
    std::string webhome( const std::string &suffix = std::string() );
}
