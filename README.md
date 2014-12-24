s&#9681;lace
======

- Solace is a modern console replacement. Written in C++03
- Solace is HTML/Ajax based.
- Solace is zlib/libpng licensed.

### Features

- Interactive REPL interface.
- Built-in code editor.
- Ajax queries.
- Logging.
- Stacktraces.
- Filtering.
- Tweakables.

### Planned

- Extending viewers for custom data.
- Histograms (inputs). VNC outputs. Monkey testing. Maybe [D3js](http://d3js.org).
- Profilers. Maybe [trace-viewer](http://github.com/google/trace-viewer).
- Code/Data visualization. Maybe [D3js](http://d3js.org).
- Save/Load Screenshots/thumbnails. Maybe any JQuery/lightbox gallery.
- Save/Load Command history.
- Framebuffer viewer. Maybe [NativeWebSocketRender](https://github.com/dwilliamson/NativeWebSocketRender).
- Stats.
- Leaks.

### Sample

```c++
~solace> cat sample.cc

#include <iostream>
#include <string>
#include <stdio.h>

#include "solace.hpp"

#ifdef _MSC_VER
#   define popen  _popen
#   define pclose _pclose
#endif

std::string evaluate( const std::string &cmd ) {
    std::string answer;
    FILE *f = popen( cmd.c_str(), "r" );
    if( f ) {
        char buff[2048];
        while( fgets(buff, 2048, f) ) answer += buff;
        pclose(f);
    }
    return answer;
}

int main() {
    // install solace html server
    solace::webinstall( 8080, &evaluate );

    // write to our log
    solace::cout << "Succesfully installed! " << 123 << std::endl;

    // capture std::cout and log it from here
    solace::capture( std::cout );
    std::cout << "Succesfully captured! " << 456 << std::endl;

    // do whatever
    for( std::string s; std::getline(std::cin, s); )
    {}

    // restore std::cout stream
    solace::release( std::cout );
}

~solace> ./a.out
Succesfully installed! 123
Succesfully captured! 456
```

### build
```
cl *.cc *.cpp -I deps deps\route66\route66.cpp deps\heal\*.cpp /Zi /DNDEBUG /Oy-
```

### possible output
![image](https://raw.github.com/r-lyeh/depot/master/solace.png)

### licenses
- Solace, zlib/libpng licensed.
- Original [Lovebird HTML template](https://github.com/rxi/lovebird) by rxi, MIT licensed.
- Original [WebDebugMenu](https://github.com/i-saint/WebDebugMenu) by i-saint, CC-BY licensed.
- Full [Ace.js code editor](http://ace.c9.io) by Ajax.org B.V., BSD licensed.
- Borrowed some ideas and javascript code from [Herman Tulleken](http://devmag.org.za/2011/01/25/make-your-logs-interactive-and-squash-bugs-faster/) and [Maxime Euzière](http://xem.github.io/Lazy/), both unlicensed.
