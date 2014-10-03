// solace - a console replacement. forked and rewritten from https://github.com/rxi/lovebird
// - rlyeh, boost licensed

#include <stdlib.h>
#include <cctype>
#include <ctime>

#include <algorithm> // std::transform
#include <ctime>
#include <deque>
#include <fstream>
#include <iostream>
#include <locale>    // std::tolower
#include <map>
#include <sstream>
#include <utility>
#include <vector>

#include <math.h>

#ifdef _MSC_VER
#include <omp.h>      // omp_get_wtime()
#else
//#include <mpi.h>
//#define omp_get_wtime MPI_Wtime
#include <sys/time.h>
double omp_get_wtime() {
struct timeval tim;
gettimeofday(&tim, NULL);
double t0=tim.tv_sec+(tim.tv_usec/1000000.0);
return t0;    
}

#endif

#include <cstdlib>
#include <heal/heal.hpp>
#include <route66/route66.hpp>
#include <oak/oak.hpp>
#include "solace.hpp"

#ifdef _WIN32
#include <io.h>       // _access()
#include <direct.h>   // _mkdir()
#else
#include <unistd.h>   // access()
#include <sys/stat.h> // mkdir() 
#endif

#ifdef min
#undef min
#endif

#ifdef max
#undef max
#endif

extern const std::string html_8080;

namespace solace {
namespace {
    enum style { INPUT_STYLE, ALERT_STYLE, REGULAR_STYLE };

    struct info {
        unsigned line;
        style type;
        int count;
        bool allowhtml;
        bool timestamp;
        char stamp[20];
        heal::callstack callstack;

        info( style type = REGULAR_STYLE ) : type(type), count(1), allowhtml(false), timestamp(true)
        {
            callstack.save();

            static unsigned line = 0;
            this->line = ++line;

            static double epoch = omp_get_wtime();

            if( timestamp ) {
                std::time_t date = std::time(NULL);

                struct std::tm *t = std::localtime(&date);
                std::strftime(stamp, sizeof(stamp)-1, "%H:%M:%S", t);

                //sprintf(&stamp[8], "+%08.3fs", fmod( omp_get_wtime() - epoch, 10000. ) );
                sprintf(&stamp[8], ".%03d", int(1000 * fmod( omp_get_wtime() - epoch, 1. )) );
            }
        }
    };
}
}

namespace solace  {
namespace config {
    enum { MAX_LINES = 1000 };
    std::map< std::string, std::string > highlights;
    std::string server_url;
    std::string (*eval)( const std::string &cmd ) = 0;
    info *the_info = 0;
}
}

namespace
{
    //
    // taken from https://github.com/r-lyeh/wire
    //
    std::string replace( std::string s, const std::string &target, const std::string &repl ) {
        for( size_t it = 0, tlen = target.length(), rlen = repl.length(); ( it = s.find( target, it ) ) != std::string::npos; it += rlen ) {
            s.replace( it, tlen, repl );
        }
        return s;
    };
    std::string replace( std::string s, const std::map< std::string, std::string > &reps ) {
        for( std::map< std::string, std::string >::const_iterator it = reps.begin(), end = reps.end(); it != end; ++it ) {
            s = replace( s, it->first, it->second );
        }
        return s;
    }
    std::deque< std::string > tokenize( const std::string &self, const std::string &delimiters ) {
        std::string map( 256, '\0' );
        for( std::string::const_iterator it = delimiters.begin(), end = delimiters.end(); it != end; ++it ) {
            unsigned char ch( *it );
            map[ ch ] = '\1';
        }
        std::deque< std::string > tokens(1);
        for( std::string::const_iterator it = self.begin(), end = self.end(); it != end; ++it ) {
            unsigned char ch( *it );
            /**/ if( !map.at(ch)          ) tokens.back().push_back( char(ch) );
            else if( tokens.back().size() ) tokens.push_back( std::string() );
        }
        while( tokens.size() && !tokens.back().size() ) tokens.pop_back();
        return tokens;
    }
    std::string join( const std::deque< std::string > &self, const char &sep ) {
        std::string out;
        for( std::deque< std::string >::const_iterator it = self.begin(), end = self.end(); it != end; ++it ) {
            out += (*it) + sep;
        }
        return out.empty() ? out + sep : out;
    }
    template<typename T>
    std::string to_string( const T &t ) {
        std::stringstream ss;
        return ss << t ? ss.str() : std::string();
    }
    template<typename T>
    std::string to_string( const T &t, const char *fmt ) {
        char buf[64];
        sprintf( buf, fmt, t );
        return buf;
    }
    std::string lowercase( std::string s ) {
        std::transform( s.begin(), s.end(), s.begin(), (int(*)(int)) std::tolower );
        return s;
    }
    //
    // taken from https://github.com/r-lyeh/DrEcho
    //
    typedef std::vector< std::string > strings;
    std::vector< std::string > split( const std::string &string, const std::string &delimiters ) {
        std::string str;
        std::vector< std::string > tokens;
        for( std::string::const_iterator it = string.begin(), end = string.end(); it != end; ++it ) {
            const char &ch = (*it);
            if( delimiters.find_first_of( ch ) != std::string::npos ) {
                if( str.size() ) tokens.push_back( str ), str = "";
                tokens.push_back( std::string() + ch );
            } else str += ch;
        }
        return str.empty() ? tokens : ( tokens.push_back( str ), tokens );
    }
    std::vector<std::string> split( const std::string &text ) {
        return split(lowercase(text), "!\"#~$%&/(){}[]|,;.:<>+-/*@'\"\t\n\\ ");
    }
    //
    // taken from https://github.com/r-lyeh/apathy
    //
    bool append( const std::string &path, const std::string &pathfile, const std::string &data ) {
        std::fstream is( (path+pathfile).c_str(), std::ios::out|std::ios::binary|std::ios::app|std::ios::ate );

        if( is.is_open() && is.good() ) {
            is.write( data.c_str(), data.size() );
        }

        return !is.fail();
    }
    bool overwrite( const std::string &path, const std::string &pathfile, const std::string &data ) {
        // is trunc flag needed?
        std::ofstream is( (path+pathfile).c_str(), std::ios::out|std::ios::binary|std::ios::trunc );

        if( is.is_open() && is.good() )
            is.write( data.c_str(), data.size() );

        return !is.fail();
    }
    // taken from...
    bool mkdirr( const std::string &path ) {
        std::string off;
        std::deque<std::string> tok = tokenize( path, "/\\" );
        for( std::deque<std::string>::const_iterator it = tok.begin(), end = tok.end(); it != end; ++it ) {
#ifdef _WIN32
            off += *it + "\\";
            _mkdir( off.c_str() );
#else
            off += *it + "/";
            mkdir( off.c_str(), 0777 );
#endif
        }
#ifdef _WIN32
        return -1 != _access( off.c_str(), 0 );
#else
        return -1 != access( off.c_str(), F_OK );
#endif
    }
    //
    // taken from https://github.com/r-lyeh/apathy 
    //
    namespace apathy
    {
        std::deque<std::string> split( const std::string &str, char sep )
        {
            std::deque<std::string> tokens;
            tokens.push_back( std::string() );

            for( std::string::const_iterator it = str.begin(), end = str.end(); it != end; ++it )
            {
                if( *it == sep )
                {
                    tokens.push_back( std::string() + sep );
                    tokens.push_back( std::string() );
                }
                else
                {
                    tokens.back() += *it;
                }
            }

            return tokens;
        }

        class sbb : public std::streambuf
        {
            public:

            typedef void (*proc)( bool open, bool feed, bool close, const std::string &text );
            typedef std::set< proc > set;
            set cb;

            sbb()
            {}

            sbb( const sbb &other ) {
                operator=(other);
            }

            sbb &operator=( const sbb &other ) {
                if( this != &other ) {
                    cb = other.cb;
                }
                return *this;
            }

            sbb( void (*cbb)( bool, bool, bool, const std::string & ) ) {
                insert( cbb );
            }

            ~sbb() {
                clear();
            }

            void log( const std::string &line ) {
                if( !line.size() )
                    return;

                std::deque<std::string> lines = split( line, '\n' );

                for( set::iterator jt = cb.begin(), jend = cb.end(); jt != jend; ++jt )
                    for( std::deque<std::string>::iterator it = lines.begin(), end = lines.end(); it != end; ++it )
                    {
                        if( *it != "\n" )
                            (**jt)( false, false, false, *it );
                        else
                            (**jt)( false, true, false, std::string() );
                    }
            }

            virtual int_type overflow( int_type c = traits_type::eof() ) {
                return log( std::string() + (char)(c) ), 1;
            }

            virtual std::streamsize xsputn( const char *c_str, std::streamsize n ) {
                return log( std::string( c_str, (unsigned)n ) ), n;
            }

            void clear() {
                for( set::const_iterator jt = cb.begin(), jend = cb.end(); jt != jend; ++jt ) {
                    (*jt)( false, false, true, std::string() );
                }
                cb.clear();
            }

            void insert( proc p ) {
                if( !p )
                    return;

                // make a dummy call to ensure any static object of this callback are deleted after ~sbb() call (RAII)
                p( 0, 0, 0, std::string() );
                p( true, false, false, std::string() );

                // insert into map
                cb.insert( p );
            }

            void erase( proc p ) {
                p( false, false, true, std::string() );
                cb.erase( p );
            }
        };

        struct captured_ostream {
            std::streambuf *copy;
            sbb sb;
            captured_ostream() : copy(0)
            {}
        };

        std::map< std::ostream *, captured_ostream > loggers;

        namespace ostream
        {
            void attach( std::ostream &_os, void (*custom_stream_callback)( bool open, bool feed, bool close, const std::string &line ) )
            {
                std::ostream *os = &_os;

                ( loggers[ os ] = loggers[ os ] );

                if( !loggers[ os ].copy )
                {
                    // capture ostream
                    loggers[ os ].copy = os->rdbuf( &loggers[ os ].sb );
                }

                loggers[ os ].sb.insert( custom_stream_callback );
            }

            void detach( std::ostream &_os, void (*custom_stream_callback)( bool open, bool feed, bool close, const std::string &line ) )
            {
                std::ostream *os = &_os;

                attach( _os, custom_stream_callback );

                loggers[ os ].sb.erase( custom_stream_callback );

                if( !loggers[ os ].sb.cb.size() )
                {
                    // release original stream
                    os->rdbuf( loggers[ os ].copy );
                }
            }

            void detach( std::ostream &_os )
            {
                std::ostream *os = &_os;

                ( loggers[ os ] = loggers[ os ] ).sb.clear();

                // release original stream
                os->rdbuf( loggers[ os ].copy );
            }

            std::ostream &make( void (*proc)( bool open, bool feed, bool close, const std::string &line ) )
            {
                static struct container
                {
                    std::map< void (*)( bool open, bool feed, bool close, const std::string &text ), sbb > map;
                    std::vector< std::ostream * > list;

                    container()
                    {}

                    ~container()
                    {
                        for( std::vector< std::ostream * >::const_iterator
                                it = list.begin(), end = list.end(); it != end; ++it )
                            delete *it;
                    }

                    std::ostream &insert( void (*proc)( bool open, bool feed, bool close, const std::string &text ) )
                    {
                        ( map[ proc ] = map[ proc ] ) = sbb(proc);

                        list.push_back( new std::ostream( &map[proc] ) );
                        return *list.back();
                    }
                } _;

                return _.insert( proc );
            }
        } // ns ::apathy::ostream
    } // ns ::apathy
}

namespace solace {
namespace {

    std::string get_tagline( const std::string &text, const std::string &extra = std::string() ) {
        // @todo: find a better (faster) way to do this {
        std::vector< std::string > tags = split( text ); //, " !|:;,.@#~$%&/(){}[]+-*\\" );
        std::string tagline; 
        for( std::vector< std::string >::const_iterator it = tags.begin(), end = tags.end(); it != end; ++it ) {
            const std::string &t = *it;
            if( t.size() > 1 ) tagline += std::string() + "class_" + t + " ";
        }
        // }
        return std::string() + "class_ALL " + tagline + extra;
    }

    std::string html( const std::string &text = std::string(), const info &i = info() );

    std::deque< std::string > log;

    std::string get_buffer() {
        std::string out;
        for( std::deque<std::string>::iterator it = log.begin(), end = log.end(); it != end; ++it ) {
            out += *it;
        }
        return out;
    }

    struct var {
        int type;
        union {
            int integer;
            double real;
            std::string *string;
        };

        void cleanup();

        var() : type(0), integer(0)
        {}

        var( const int &i ) : type(0), integer(i)
        {}

        var( const double &r ) : type(1), real(r)
        {}

        var( const std::string &r ) : type(2), string( new std::string(r) )
        {}

        template<size_t N>
        var( const char (&s)[N]) : type(2), string( new std::string(s) )
        {}

        var( const var &other ) {
            operator=( other );
        }

        ~var() {
            cleanup();
        }

        var &operator=( const var &other ) {
            if( &other != this ) {
                cleanup();
                type = other.type;
                if( type == 0 ) integer = other.integer;
                if( type == 1 ) real = other.real;
                if( type == 2 ) string = new std::string( *other.string );
            }
            return *this;
        }
    };

    template<typename T> bool isType(const var &v) { return false; }
    template<>           bool isType<int>(const var &v) { return v.type == 0; }
    template<>           bool isType<double>(const var &v) { return v.type == 1; }
    template<>           bool isType<std::string>(const var &v) { return v.type == 2; }

    template<typename T> const T& cast(const var &v) { static T t; return t = T(); }
    template<>           const int& cast<int>(const var &v) { return v.integer; }
    template<>           const double& cast<double>(const var &v) { return v.real; }
    template<>           const std::string& cast<std::string>(const var &v) { return *v.string; }

    void var::cleanup() {
        if(isType<std::string>(*this)) delete string, string = 0;
        type = 0;
    }

    oak::tree< std::string, var > tree_env;

    std::string get_env( const oak::tree< std::string, var > &env, const std::string &path = "" ) {
        std::stringstream ss, table;
        ss << "{ \"valid\": true, \"path\": \"" << path << "\", \"vars\":[ ";
            for( oak::tree< std::string, var >::const_iterator it = env.begin(), end = env.end(); it != end; ++it ) {
                const std::pair< std::string, oak::tree< std::string, var > > &pair = *it;
                if( pair.second.size() ) {
                    std::string tt = get_env( pair.second, path + pair.first );
                    ss << "{\"key\":\"" << pair.first << "\", \"value\": \"[table]\", \"type\": \"table\" }, ";
                } else {
                    const var &v = pair.second.get();
                    /****/ if( isType<std::string>(v) ) {
                        ss << "{\"key\":\"" << pair.first << "\", \"value\": \""<< cast<std::string>(v) << "\", \"type\": \"string\" }, ";
                    } else if( isType<int>(v) ) {
                        ss << "{\"key\":\"" << pair.first << "\", \"value\": " << cast<int>(v) << ", \"type\": \"number\" }, ";
                    } else if( isType<double>(v) ) {
                        ss << "{\"key\":\"" << pair.first << "\", \"value\": " << cast<double>(v) << ", \"type\": \"number\" }, ";
                    }
                }
            }
            ss << "{\"key\":\"\", \"value\":\"\", \"type\": \"string\"}";
        ss << " ] }" << std::endl;
        return ss.str();
    }

    std::string index_8080() {
        std::stringstream html;

        std::ifstream ifs("8080/index.html", std::ios::binary);
        if( ifs.good() ) {
            html << ifs.rdbuf();
        } else {
            html << html_8080;

            ifs.close();
            std::ofstream ofs("8080/index.html", std::ios::binary);
            if( ofs.good() ) {
                ofs << html.str();
            }
        }

        std::map< std::string, std::string > map;
        map[ "{TITLE}" ] = "S&#9681;LACE"; //SOLACE";
        map[ "{SUBTITLE}" ] = "1.0.0 (built " __DATE__ " " __TIME__ ")";
        map[ "{URL}" ] = "https://github.com/r-lyeh/solace";
        map[ "{BUFFER}" ] = get_buffer();
        map[ "{TIMEOUT}" ] = "1.0";

        return replace(html.str(), map);
    }

    std::string html( const std::string &text, const info &i ) {
        std::string str = text;

        if( !i.allowhtml ) {
            str = replace( str, "<", "&lt;" );
            str = replace( str, ">", "&gt;" );
            str = replace( str, "\n", "<br>" );
        }

        std::string prefix, tagline = get_tagline(str);

        /**/ if( i.type == ALERT_STYLE ) {
            prefix = "<span class=\"errormarker\">!</span> ";
        }
        else if( i.type == INPUT_STYLE ) {
            prefix = "<span class=\"inputline\">" + str + "</span>";
            str = "";
        }
        if( i.count > 1 ) {
            prefix = prefix + "<span class=\"repeatcount repeatcount_light\">" + to_string( i.count ) + "</span>";
        }
        if( i.timestamp ) {
            prefix = "<span class=\"timestamp\">[" + std::string(i.stamp) + "]</span> " + prefix;
        }

        std::string linenum = "L" + to_string(i.line, "%04d");
        std::string stack = std::string() + "<span id=\"" + linenum + "\"></span>";

        std::string json("[");
        for( std::vector<void*>::const_iterator it = i.callstack.frames.begin(), end = i.callstack.frames.end(); it != end; ++it ) {
            const void *f = *it;
            json += to_string(f, "%p") + ',';
        }
        json[ json.size() - 1 ] = ']';

        std::string L = std::string() + "<a href=\"#\" class=\"monospaced\" onclick=\"trace('" +linenum+ "','" +json+ "')\">" + linenum + "</a> ";

        str = "<span class=\"" + tagline + "\">" + L + prefix + str + stack + "<br></span>";
        return str;
    };

    int GET_root( route66::request &req, std::ostream &headers, std::ostream &content ) {
        if( req.uri.size() <= 2 || req.uri == "index.html" ) req.uri = "/index.html";

        if( req.uri == "/index.html" ) {
            headers << route66::mime(".html");
            content << index_8080() << std::endl;
            return 200;
        } else {
            headers << route66::mime(req.uri);
            std::ifstream ifs( (solace::webhome("8080/") + req.uri).c_str() );
            if( ifs.good() ) return content << ifs.rdbuf() << std::endl, 200;
            else return content << "404", 404;
        }
    }

    int GET_env_json( route66::request &req, std::ostream &headers, std::ostream &content ) {

        if( req.arguments.find("p") == req.arguments.end() ) {
            headers << route66::mime(".json");
            content << std::endl;
            return 404;
        }

        std::deque<std::string> split = tokenize(req.arguments["p"], ".");

        std::string random = to_string( rand() );
        tree_env[ "random" ] = random;
        tree_env[ "hello" ] = "world";
        tree_env[ "abc" ][ "def" ] = 123;

        oak::tree< std::string, var > const *env = &tree_env;
        for( std::deque<std::string>::const_iterator it = split.begin(), end = split.end(); it != end; ++it ) {
            const std::string &path = *it;
            env = &(*env)(path);
        }

        headers << route66::mime(".json");
        content << get_env( env->is_valid() ? *env : tree_env, join(split, '.') );
        return 200;
    }

    int POST( route66::request &req, std::ostream &headers, std::ostream &content ) {
        std::string cmd = req.arguments["input"], answer;

        info *&the_info = config::the_info;
        the_info = new info();
        the_info->type = INPUT_STYLE;

        ::solace::cout << ( std::string("> ") + cmd ) << std::endl;

        /**/ if( cmd.substr(0,5) == "/help" ) {
            the_info->allowhtml = true;
            answer = "/help ; /url url ; /img url ; /print text...";
        }
        else if( cmd.substr(0,5) == "/url " ) {
            the_info->allowhtml = true;
            answer = "<iframe src=\"" + cmd.substr(5) + "\" style=\"border:0; width:100%; height:100%;\" >error: iframes are disabled</iframe>";
        }
        else if( cmd.substr(0,5) == "/img " ) {
            the_info->allowhtml = true;
            answer = "<img src=\"" + cmd.substr(5) + "\" />";
        }
        else if( cmd.substr(0,7) == "/print " ) {
            the_info->allowhtml = true;
            the_info->type = REGULAR_STYLE;
            answer = cmd.substr(7);
        }
        else {
            if( ::solace::config::eval ) {
                answer = (*::solace::config::eval)( cmd );
            }
        }

        if( answer.size() ) {
            ::solace::cout << ( std::string("> ") + answer ) << std::endl;
        } 

        delete the_info, the_info = 0;

        headers << route66::mime(".html");
        content << std::endl;

        return 200;
    }

    int GET_buffer( route66::request &req, std::ostream &headers, std::ostream &content ) {
        headers << route66::mime(".html");
        content << get_buffer() << std::endl;
        return 200;
    }

    int GET_fread( route66::request &req, std::ostream &headers, std::ostream &content ) {
        std::ifstream ifs( req.arguments["fp"].c_str() );
        if( ifs.good() ) {
            headers << route66::mime(".text");
            content << ifs.rdbuf() << std::endl;
            return 200;
        } else {
            headers << route66::mime(".text");
            content << "404: not found" << std::endl;
            return 404;
        }
    }

    int POST_fwrite( route66::request &req, std::ostream &headers, std::ostream &content ) {
        for( std::map<std::string,std::string>::const_iterator it = req.multipart.begin(), end = req.multipart.end(); it != end; ++it ) {
            const std::pair<std::string, std::string> &file = *it;
            #ifdef _WIN32
                std::string filename = replace( file.first, "/", "\\" );
            #else
                std::string filename = replace( file.first, "\\", "/" );
            #endif
            std::ofstream ofs( filename.c_str(), std::ios::binary );
            if( !ofs.good() ) {
                headers << route66::mime(".text");
                content << "404: cannot write '" << file.first << "'" << std::endl;
                return 404;
            }
            ofs.write( file.second.c_str(), file.second.size() );
        }
        headers << route66::mime(".text");
        content << "ok" << std::endl;
        return 200;
    }

    int GET_stacktrace( route66::request &req, std::ostream &headers, std::ostream &content ) {
        std::string cmd = req.arguments["p"];
        std::deque<std::string> strings = tokenize( cmd, "[,]");
        heal::callstack cs;
        cs.frames.resize(0);
        for( std::deque<std::string>::const_iterator it = strings.begin(), end = strings.end(); it != end; ++it ) {
            const std::string &s = *it;
            //std::stringstream ss;
            //ss << s;
            void *p;
            if(1!=sscanf(s.c_str(),"%p",&p)) p = 0;
            cs.frames.push_back(p); //ss >> p ? p : 0);
        }

        cmd.clear();
        std::vector<std::string> traces = cs.unwind();
        for( std::vector<std::string>::iterator jt = traces.begin(), jend = traces.end(); jt != jend; ++jt ) {
            std::string &it = *jt;
            std::deque<std::string> split = tokenize(replace(it, "\\", "/"), "()"); // expects "symbol (file:line)" stacktrace format
            if( split.size() == 2 )
                it = std::string() + "<a href=\"javascript:edit('" + split[1] + "');\"><xml>" + it + "</xml></a>\n";
            else
                it = std::string() + it + "\n";

            cmd += it;
        }

        headers << route66::mime(".text");
        content << cmd << std::endl;

        return 200;
    }

    void terminal_logger( const std::string &cache, bool opening = false ) {
        if( opening ) {
        }
        fputs( cache.c_str(), stdout );
        fputs( "\n", stdout );
    }

    void file_logger( const std::string &cache, bool opening = false ) {
        static std::string path = solace::webhome("8080/");
        static std::string filename;
        if( opening ) {
            if( filename.empty() ) {
                char text[100];
                time_t now = std::time(NULL);
                struct tm *t = localtime(&now);
                std::strftime(text, sizeof(text)-1, ".solace.%Y%m%d-%H%M%S.log", t);

                filename = text;
                append( path, ".solace.toc.ajax", std::string()+"<option value=\""+filename+"\">\n" );
            }
        }
        append( path, filename, cache );
    }

    void solace_logger( const std::string &cache, bool opening = false ) {
        if( opening ) {
        }
        log.push_back( cache );
        if( log.size() > ::solace::config::MAX_LINES ) {
            log.pop_front();
        }
    }

    void logger( bool open, bool feed, bool close, const std::string &line ) {
        static std::string cache;

        if( open ) {
            std::string raw = "Ready. Type /help for a tour.";
            std::string text = html( raw, info(ALERT_STYLE) );

            terminal_logger( raw, true );
            file_logger( text, true );
            solace_logger( text, true );
        }
        else
        if( close ) {
        }
        else
        if( feed ) {
            if( cache.empty() )
                return;

            info i = config::the_info ? *config::the_info : info();

            std::deque< std::string > lines = tokenize(cache, "\r\n");
            for( std::deque< std::string >::iterator it = lines.begin(), end = lines.end(); it != end; ++it ) {
                std::string &raw = *it;
                terminal_logger( raw );

                std::string text = html( raw, i );
                file_logger( text );
                solace_logger( text );
            }

            cache = std::string();
        }
        else {
            cache += line;
        }
    }
}
} // ::

namespace solace 
{
    void set_highlights( const std::vector< std::string > &highlights ) {
        std::string colors, code;

        code += std::string()+"<input type=\"button\" value=\"ALL\" class=\"Button\" onclick=\"toggleClass(this,'repeatcount'); var i = hasClass(this, 'repeatcount') ^ 1; setVis('class_ALL', i); each($('.Button'), function(){ if(!i) addClass(this, 'repeatcount'); else removeClass(this, 'repeatcount'); }); \"/>\n";

        for( size_t i = 0; i < highlights.size(); ++i ) {
            std::string lo( lowercase( highlights[i] ) );

            int slice( i ), max( highlights.size() );
            float hue = slice * ( 1.f / max );
            if( hue > 1.f ) hue = hue - 1.f;

            colors += std::string()+".class_"+lo+" { background-color: hsl("+ to_string(int(hue*360)) + ",50%,50%); }\n";
            code += std::string()+"<input type=\"button\" value=\""+lo+"\" class=\"class_"+lo+" Button\" onclick=\"toggleVis('class_"+lo+"'); toggleClass(this,'repeatcount');\"/>\n";
        }
        overwrite( solace::webhome("8080/"), ".solace.colors.css", colors );
        overwrite( solace::webhome("8080/"), ".solace.interface.ajax", code );
    }

    bool webopen() {
        if( !config::server_url.empty() ) {
#if   defined(_WIN32)
            return system( (std::string() + "start " + config::server_url ).c_str() ), true;
#elif defined(__linux__)
            return system( (std::string() + "xdg-open " + config::server_url + " 2>/dev/null &" ).c_str() ), true;
#elif defined(__APPLE__)
            return system( (std::string() + "open " + config::server_url + " 2>/dev/null &" ).c_str() ), true;
#endif
        }
        return false;
    }

    bool webinstall( int port, std::string (*fn)( const std::string &cmd ) ) {

        {
            // init unwinding from main thread
            heal::callstack cs;
            cs.save();
            std::string unused = cs.flat();
            std::cout << unused << std::endl;
        }

        bool ok = true;

        // config script/expression evaluator
        config::eval = fn;

        // add a web server, with env in ajax
        ok = ok && route66::create( port, "GET *", GET_root );
        ok = ok && route66::create( port, "GET /env.json", GET_env_json );
        ok = ok && route66::create( port, "POST /", POST );
        ok = ok && route66::create( port, "GET /buffer", GET_buffer );

        // callstack & lookup service
        ok = ok && route66::create( port, "GET /stacktrace.text", GET_stacktrace );
        ok = ok && route66::create( port, "GET /fread", GET_fread );
        ok = ok && route66::create( port, "POST /fwrite", POST_fwrite );

        //
        if( ok ) { config::server_url = "http://localhost:" + to_string(port); }

        return ok;
    }

    std::string webhome( const std::string &suffix ) {
#ifdef _WIN32
        std::string at = std::string( std::getenv("APPDATA") ? std::getenv("APPDATA") : ".") + "\\.solace\\" + suffix;
#else
        std::string at = std::string( std::getenv("HOME") ? std::getenv("HOME") : ".") + "/.solace/" + suffix;
#endif
        static std::set< std::string > cache;
        if( cache.find(at) == cache.end() ) {
            cache.insert(at);
            mkdirr(at);
        }
#ifdef _WIN32
        return replace(at, "/", "\\");
#else
        return replace(at, "\\", "/");
#endif
    }

    //
    // taken from https://github.com/r-lyeh/DrEcho
    //
    namespace {
        std::set< std::ostream * > captured;
    }

    std::ostream &cout = apathy::ostream::make(logger);

    bool capture( std::ostream &os_ ) {
        std::ostream *os = &os_;
        if( os != &solace::cout ) {
            if( solace::captured.find(os) == solace::captured.end() ) {
                solace::captured.insert(os);
                apathy::ostream::attach( *os, logger );
                return true;
            }
        }
        return false;
    }

    bool release( std::ostream &os_ ) {
        std::ostream *os = &os_;
        if( os != &solace::cout ) {
            if( solace::captured.find(os) != solace::captured.end() ) {
                solace::captured.erase(os);
                apathy::ostream::detach( *os );
                return true;
            }
        }
        return false;
    }
}

#define $QUOTE(...) std::string(#__VA_ARGS__)
const std::string html_8080 = $QUOTE(
<!--
  -- lovebird
  --
  -- Copyright (c) 2014, rxi
  --
  -- This library is free software; you can redistribute it and/or modify it
  -- under the terms of the MIT license. See LICENSE for details.
  --
  -- /* https://github.com/rxi/lovebird */
  -->

<!doctype html>
<html lang="en">
  <head>
  <meta http-equiv="x-ua-compatible" content="IE=Edge" charset="utf-8" />
  <title>{TITLE}</title>

<link rel='stylesheet' type='text/css' href='.solace.colors.css' />
<script type="text/javascript" src="https://cdn.rawgit.com/ajaxorg/ace-builds/master/src-noconflict/ace.js" charset="utf-8"></script>

<link rel="stylesheet" type='text/css' href="https://rawgit.com/t4t5/sweetalert/master/lib/sweet-alert.css">
<script type="text/javascript" src="https://cdn.rawgit.com/t4t5/sweetalert/master/lib/sweet-alert.min.js"></script>

<script language='javascript'>

  /* https://github.com/xem/Lazy lazy.min.js (0.7 KiB) */
  $w=window,$n=navigator,$d=document,$r=$d.documentElement,$h=$("head")[0],$b=$("body")[0],$ie=$n.appVersion.match(/MSIE ([\\d.]+)/),$ie=$ie?$ie[1]:0;$m=0<=$n.userAgent.indexOf("mobile");"AbbrArticleAsideAudioCanvasDetailsFigureFooterHeaderHgroupMarkMenuMeterNavOutputProgressSectionTimeVideo".replace(/.[a-z]+/g,function(a){document.createElement(a)});$w.getComputedStyle||(window.getComputedStyle=function(a){this.el=a;this.getPropertyValue=function(b){var c=/(\\-([a-z]){1})/g;"float"==b&&(b="styleFloat");c.test(b)&&(b=b.replace(c,function(a,b,c){return c.toUpperCase()}));return a.currentStyle[b]?a.currentStyle[b]:null};return this});function $(a){if("#"==a.charAt(0))return $d.getElementById(a.substr(1));if("."==a.charAt(0)){if($d.getElementsByClassName)return $d.getElementsByClassName(a.substr(1));for(var b=[],a=RegExp("(^| )"+a.substr(1)+"( |$)"),c=$("*"),d=0,e=c.length;d<e;d++)a.test(c[d].className)&&b.push(c[d]);return b}return $d.getElementsByTagName(a)}function remove(a){a.parentNode.removeChild(a)}function on(a,b,c){a.addEventListener?a.addEventListener(b,c,!1):a.attachEvent("on"+b,c)}function off(a,b,c){a.removeEventListener?a.removeEventListener(b,c):a.detachEvent("on"+b,c)}function css(a,b,c){if(c)a.style[b]=c;else return $w.getComputedStyle(a).getPropertyValue(b)}function each(a,b){var c,l=a.length;for(c=0;c<l;c++)b.call(a[c])}

    /* Functions for toggling visibility in the log. */
    /* Gets all the elements in the node of the given class and tag. */
    function getElementsByClass(searchClass,node,tag) {
            var classElements = new Array();
            if ( node == null ) node = document;
            if ( tag == null ) tag = '*';
            var elements = node.getElementsByTagName(tag);
            var elementsSize = elements.length;
            var pattern = new RegExp("(^|\\s)" + searchClass + "(\\s|$)");
            for (i = 0, j = 0; i < elementsSize; i++) {
                    if ( pattern.test(elements[i].className) ) {
                            classElements[j] = elements[i];
                            j++;
            }}
            return classElements;
    };
    /* Toggles visibility of stacks in the HTML log. */
    function style(id) {
            return document.getElementById(id).style;
    }
    function show(id) {
            style(id).display = 'inline';
    };
    function hide(id) {
            style(id).display = 'none';
    };
    function vis(id) {
            return style(id).display;
    };
    function toggle(id) {
            if( vis(id) == 'inline' ) hide(id); else show(id);
    }

    /* Toggles visibility of a class in the HTML log. */
    function toggleVis(className) {
            var elements = getElementsByClass(className);
            var pattern = new RegExp("(^|\\s)Button(\\s|$)");
            for(i = 0; i < elements.length; i++) {
                    if(!pattern.test(elements[i].className)) {
                            if(elements[i].style.display != 'none') elements[i].style.display = 'none';
                            else elements[i].style.display = 'inline'
    }}};
    function setVis(className, kind) {
            kind = kind ? 'inline' : 'none';
            var elements = getElementsByClass(className);
            var pattern = new RegExp("(^|\\s)Button(\\s|$)");
            for(i = 0; i < elements.length; i++) {
                    if(!pattern.test(elements[i].className)) {
                            elements[i].style.display = kind;
    }}};

    function addClass(element, classToAdd) {
        var currentClassValue = element.className;

        if (currentClassValue.indexOf(classToAdd) == -1) {
            if ((currentClassValue == null) || (currentClassValue === "")) {
                element.className = classToAdd;
            } else {
                element.className += " " + classToAdd;
            }
        }
    };

    function hasClass(element, className) {
        var currentClassValue = element.className;

        if (currentClassValue == className) {
            return true;
        }
        else {
          var classValues = currentClassValue.split(" ");

          for (var i = 0 ; i < classValues.length; i++) {
              if (className == classValues[i]) {
                  return true;
              }
          }
        }

        return false;
    };

    function removeClass(element, classToRemove) {
        var currentClassValue = element.className;
        var found = 0;

        if (currentClassValue == classToRemove) {
            element.className = "";
            found++;
        }
        else {
          var classValues = currentClassValue.split(" ");
          var filteredList = [];

          for (var i = 0 ; i < classValues.length; i++) {
              if (classToRemove != classValues[i]) {
                  filteredList.push(classValues[i]);
              } else {
                  found ++;
              }
          }

          element.className = filteredList.join(" ");
        }

        return found;
    };

    function toggleClass(element, className) {
      if( !hasClass(element, className) ) addClass(element, className);
      else removeClass(element, className);
    };

    function Query(selector) {
      var element = document.querySelector(selector);
      return element;
    };


</script>

<style type="text/css" media="screen">
    )+'#'+$QUOTE(editorwin {
        position: absolute;
        top: 0;
        right: 0;
        bottom: 0;
        left: 0;
        text-align: center;
    }
    )+'#'+$QUOTE(editor {
        text-align: left;
        position: absolute;
        top: 20px;
        right: 0;
        bottom: 0;
        left: 0;
    }
</style>

<style type='text/css'>
    pre {
        display:none;
    }
    .black_bg {
        background-color:#000000;
    };
    a {
        color:#ff0000;
        background-color:#ffffff;
        padding:3px;
        font-size:small;
        text-decoration: none;
        width:5em;
        font-weight:normal;
        cursor: hand;
        border-radius:4px;
    }
    h1 {
        padding-top: 3em;
    }
    body {
        font-family:"Calibri";
        padding-top: 0;
        margin-top: 0;
    }
    p {
        font-family:"Calibri";
        padding:4px;
        border-radius:5px;
        margin:2px;
        display:block;
    }
    img {
        float: none;
    }
    .Header {
        position:fixed;
        background:#ffffff;
        padding-bottom:1em;
    }
    .Padding {
        height:20em;
    }

    .time {
        float: left;
        width:10em;
        background-color:transparent;
    }
    .Button {
        width:5em;
        border:none;
        border-radius: 5px;
        padding:4px;
        margin:2px;
        cursor: pointer;
        vertical-align: middle;
        font-family: "Calibri", helvetica, verdana, sans;
    }
    .Button2 {
      padding-top: 0px;
      padding-bottom: 0px;
      vertical-align: middle;
    }
</style>

) +
$QUOTE(

  <style>
    body {
      margin: 0px;
      font-size: 15px;
      font-family: "Calibri", helvetica, verdana, sans;
      /* font-weight: 600; */
      background: #FFFFFF;
    }
    form {
      margin-bottom: 0px;
    }

    iframe {
      border: 0; width: 100%; height: 100%;
    }

    .timestamp {
      color: #909090;
      padding-right: 4px;
    }
    .repeatcount_light {
      color: #F0F0F0;
      background: #505050;
    }
    .repeatcount {
      font-size: 11px;
      font-weight: bold;
      text-align: center;
      padding-left: 4px;
      padding-right: 4px;
      padding-top: 0px;
      padding-bottom: 0px;
      border-radius: 7px;
      display: inline-block;
      vertical-align: middle;
    }
    .errormarker {
      color: #F0F0F0;
      background: #8E0000;
      font-size: 11px;
      font-weight: bold;
      text-align: center;
      border-radius: 8px;
      width: 17px;
      padding-top: 0px;
      padding-bottom: 0px;
      display: inline-block;
    }
    .greybordered {
      margin: 12px;
      background: #F0F0F0;
      border: 1px solid #E0E0E0;
      border-radius: 3px;
    }
    .monospaced,
    .inputline {
      font-family: mono, courier;
      font-size: 13px;
      color: #606060;
    }
    .inputline:before {
      content: '\00B7\00B7\00B7';
      padding-right: 5px;
    }
    .errorline {
      color: #8E0000;
    }
    )+'#'+$QUOTE(header {
      background: #101010;
      height: 25px;
      color: #F0F0F0;
      padding: 9px
    }
    )+'#'+$QUOTE(title {
      float: left;
      font-size: 20px;
    }
    )+'#'+$QUOTE(title a {
      color: #F0F0F0;
      text-decoration: none;
    }
    )+'#'+$QUOTE(title a:hover {
      color: #FFFFFF;
    }
    )+'#'+$QUOTE(subtitle {
      font-size: 10px;
    }
    )+'#'+$QUOTE(status {
      float: right;
      font-size: 14px;
      padding-top: 4px;
    }
    )+'#'+$QUOTE(main a {
      color: #000000;
      text-decoration: none;
      background: #E0E0E0;
      border: 1px solid #D0D0D0;
      border-radius: 3px;
      padding-left: 2px;
      padding-right: 2px;
      display: inline-block;
    }
    )+'#'+$QUOTE(main a:hover {
      background: #D0D0D0;
      border: 1px solid #C0C0C0;
    }
    )+'#'+$QUOTE(sliders {
      position: absolute;
      top: 40px; bottom: 0px; left: 0px;
      width: 300px;
    }
    )+'#'+$QUOTE(console {
      position: absolute;
      top: 40px; bottom: 0px; left: 312px; right: 312px;
    }
    )+'#'+$QUOTE(input {
      position: absolute;
      margin: 10px;
      bottom: 0px; left: 0px; right: 0px;
    }
    )+'#'+$QUOTE(inputbox {
      width: 100%;
      font-family: mono, courier;
      font-size: 13px;
    }
    )+'#'+$QUOTE(output {
      overflow-y: scroll;
      position: absolute;
      margin: 10px;
      line-height: 17px;
      top: 0px; bottom: 36px; left: 0px; right: 0px;
    }
    )+'#'+$QUOTE(env {
      position: absolute;
      top: 40px; bottom: 0px; right: 0px;
      width: 300px;
    }
    )+'#'+$QUOTE(envheader {
      padding: 5px;
      background: #E0E0E0;
    }
    )+'#'+$QUOTE(envvars {
      position: absolute;
      left: 0px; right: 0px; top: 25px; bottom: 0px;
      margin: 10px;
      overflow-y: scroll;
      font-size: 12px;
    }
  </style>
  </head>
  <body>
    <div id="header">
      <div id="title">
        <a href="{URL}" class="black_bg">{TITLE}</a>
        <span id="subtitle">{SUBTITLE}</span>
      </div>
      <div id="status"></div>
      <span id="interface"></span>
      <span>
      <input list="tocs" name="toc" oninput="load(this.value);">
      <datalist id="tocs"></datalist>
      </span>
    </div>
    <div id="main">
      <div id="console" class="greybordered">
        <div id="output">{BUFFER}</div>
        <div id="input">
          <form method="post"
                onkeydown="return onInputKeyDown(event);"
                onsubmit="onInputSubmit(); return false;">
            <input id="inputbox" name="input" type="text"></input>
          </form>
        </div>
      </div>
      <div id="env" class="greybordered">
        <div id="envheader"></div>
        <div id="envvars"></div>
      </div>
      <div id="sliders" class="greybordered">
        <iframe src="http://localhost:10002/"></iframe>
      </div>
  
      <div id="editorwin" class="greybordered" style="display:none">
        <div style="width:auto;overflow:scroll;max-height:200px;"><a href='#' id="editortitle" onclick="javascript:hide('editorwin');" style="width:100%"></a></div>
        <div id="editor"></div>
      </div>
    </div>
    <script>
      document.getElementById("inputbox").focus();

      var changeFavicon = function(href) {
        var old = document.getElementById("favicon");
        if (old) document.head.removeChild(old);
        var link = document.createElement("link");
        link.id = "favicon";
        link.rel = "shortcut icon";
        link.href = href;
        document.head.appendChild(link);
      };

      var truncate = function(str, len) {
        if (str.length <= len) return str;
        return str.substring(0, len - 3) + "...";
      };

      var geturl = function(url, onComplete, onFail) {
        var req = new XMLHttpRequest();
        req.onreadystatechange = function() {
          if (req.readyState != 4) return;
          if (req.status == 200) {
            if (onComplete) onComplete(req.responseText);
          } else {
            if (onFail) onFail(req.responseText);
          }
        };
        url += (url.indexOf("?") > -1 ? "&_=" : "?_=") + Math.random();
        req.open("GET", url, true);
        req.send();
      };

      var serialize = function(obj, prefix) {
        var str = [];
        for(var p in obj) {
          if (obj.hasOwnProperty(p)) {
            var k = prefix ? prefix + "[" + p + "]" : p, v = obj[p];
            str.push(typeof v == "object" ?
              serialize(v, k) :
              encodeURIComponent(k) + "=" + encodeURIComponent(v));
          }
        }
        return str.join("&");
      };

      var postfile = function(url, name, data, onComplete, onFail) {
        var req = new XMLHttpRequest();
        req.open("POST", url, true);
        //req.setRequestHeader('Content-Length', data.length);
        req.onreadystatechange = function() {
          if (req.readyState != 4) return;
          if (req.status == 200) {
            if (onComplete) onComplete(req.responseText);
          } else {
            if (onFail) onFail(req.responseText);
          }
        };
        var formData = new FormData();
        formData.append(name, data);
        req.send(formData);
      };

      var posturl = function(url, options, onComplete, onFail) {
        var req = new XMLHttpRequest();
        req.open("POST", url, true);
        req.setRequestHeader("Content-type","application/x-www-form-urlencoded");
        req.onreadystatechange = function() {
          if (req.readyState != 4) return;
          if (req.status == 200) {
            if (onComplete) onComplete(req.responseText);
          } else {
            if (onFail) onFail(req.responseText);
          }
        };
        var options = serialize(options);
        req.send(options);
      };

      var divContentCache = {};

      var getDivContent = function(id) {
        return document.getElementById(id).innerHTML;
      };
      var updateDivContent = function(id, content) {
        if (divContentCache[id] != content) {
          document.getElementById(id).innerHTML = content;
          divContentCache[id] = content;
          return true;
        };
        return false;
      };

      var onInputSubmit = function() {
        var b = document.getElementById("inputbox");
        var req = new XMLHttpRequest();
        req.open("POST", "/", true);
        req.send("input=" + encodeURIComponent(b.value));
        /* Do input history */
        if (b.value && inputHistory[0] != b.value) {
          inputHistory.unshift(b.value);
        };
        inputHistory.index = -1;
        /* Reset */
        b.value = "";
        refreshOutput();
      };

      /* Input box history */
      var inputHistory = [];
      inputHistory.index = 0;
      var onInputKeyDown = function(e) {
        var key = e.which || e.keyCode;
        if (key != 38 && key != 40) return true;
        var b = document.getElementById("inputbox");
        if (key == 38 && inputHistory.index < inputHistory.length - 1) {
          /* Up key */
          inputHistory.index++;
        };
        if (key == 40 && inputHistory.index >= 0) {
          /* Down key */
          inputHistory.index--;
        };
        b.value = inputHistory[inputHistory.index] || "";
        b.selectionStart = b.value.length;
        return false;
      };

      var bufferLocation = "/buffer";

      /* Output buffer and status */
      var refreshOutput = function() {
        geturl(bufferLocation, function(text) {
          updateDivContent("status", "connected &#9679;");
          if (updateDivContent("output", text)) {
            var div = document.getElementById("output");
            div.scrollTop = div.scrollHeight;
          };
          /* Update favicon */
          changeFavicon("data:image/png;base64," +
"iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAAP1BMVEUAAAAAAAAAAAD/"+"/"+"/"+"/19fUO"+
"Dg7v7+/h4eGzs7MlJSUeHh7n5+fY2NjJycnGxsa3t7eioqKfn5+QkJCHh4d+fn7zU+b5AAAAAnRS"+
"TlPlAFWaypEAAABRSURBVBjTfc9HDoAwDERRQ+w0ern/WQkZaUBC4e/mrWzppH9VJjbjZg1Ii2rM"+
"DyR1JZ8J0dVWggIGggcEwgbYCRbuPRqgyjHNpzUP+39GPu9fgloC5L9DO0sAAAAASUVORK5CYII="
          );
        },
        function(text) {
          updateDivContent("status", "disconnected &#9675;");
          /* Update favicon */
          changeFavicon("data:image/png;base64," +
"iVBORw0KGgoAAAANSUhEUgAAABAAAAAQCAMAAAAoLQ9TAAAAYFBMVEUAAAAAAAAAAADZ2dm4uLgM"+
"DAz29vbz8/Pv7+/h4eHIyMiwsLBtbW0lJSUeHh4QEBDn5+fS0tLDw8O0tLSioqKfn5+QkJCHh4d+"+
"fn5ycnJmZmZgYGBXV1dLS0tFRUUGBgZ0He44AAAAAnRSTlPlAFWaypEAAABeSURBVBjTfY9HDoAw"+
"DAQD6Z3ey/9/iXMxkVDYw0g7F3tJReosUKHnwY4pCM+EtOEVXrb7wVRA0dMbaAcUwiVeDQq1Jp4a"+
"xUg5kE0ooqZu68Di2Tgbs/DiY/9jyGf+AyFKBAK7KD2TAAAAAElFTkSuQmCC"
          );
        });

        geturl(".solace.interface.ajax", function(text) {
          updateDivContent("interface", text);
        },
        function(text) {
        });

        geturl(".solace.toc.ajax", function(text) {
          updateDivContent("tocs", text);
        },
        function(text) {
        });
      };
      setInterval(refreshOutput,
                  {TIMEOUT} * 1000);

      var load = function(prebuffer) {
        bufferLocation = prebuffer;
      };

      var alert = function(txt, ok) {
          if(ok) sweetAlert("Ok!", txt, "success");
          else //sweetAlert("Error!", txt, "error");
          sweetAlert({
             title: "Error!",
             text: txt,
             type: "error",
             //showCancelButton: true,
             confirmButtonColor: "#484848", //DD6B55",
             confirmButtonText: "OK",
             closeOnConfirm: true },
             function(){
             }
          );
      };

      var confirm = function(txt, fn) {
           sweetAlert({
             title: "Are you sure?",
             text: "Local changes will be discarded",
             type: "warning",
             showCancelButton: true,
             confirmButtonColor: "#DD6B55",
             confirmButtonText: "Yes, exit now!",
             closeOnConfirm: true },
             function(){
               fn.call();
             }
          );
          return true;
      };

      var editing = '';
      var save = function( ok_cb, err_cb ) {
          postfile('/fwrite', editing, editor.getSession().getValue(), ok_cb, err_cb );
          editor.getSession().getUndoManager().reset();
          editor.getSession().getUndoManager().markClean();
      };

      var edit = function(file_colon_line) {
        var arr = file_colon_line.split(':');
        var line = parseInt(arr[arr.length - 1]);
        var file = (arr.pop(), arr.join(':'));
        editing = file;
        geturl("/fread?fp="+file, function(text) {

          show("editorwin");
          updateDivContent("editortitle", file + " (CTRL-Q: <u>q</u>uit, CTRL-S: <u>s</u>ave doc)");

          editor.getSession().setValue("bla bla");
          editor.getSession().setValue(text);
          editor.gotoLine(line,0,false);
          editor.focus();

          editor.getSession().getUndoManager().reset();
          editor.getSession().getUndoManager().markClean();

          editor.commands.addCommand({
            name: 'nav_down.',
            bindKey: {win: 'Ctrl-S', mac: 'Command-S'},
            exec: function(editor) {
                if( editor.getSession().getUndoManager().hasUndo() == true ) {
                    save( function() { alert("document saved", 1); }, function() { alert("failed while saving"); } );
                }
            },
            readOnly: true
          });

          editor.commands.addCommand({
            name: 'escape1.',
            bindKey: {win: 'Esc', mac: 'Esc'},
            exec: function(editor) {
                if( editor.getSession().getUndoManager().hasUndo() == true )
                confirm("exit?", function() { hide("editorwin"); });
                else hide("editorwin");
            },
            readOnly: true
          });

          editor.commands.addCommand({
            name: 'escape2.',
            bindKey: {win: 'Ctrl-Q', mac: 'Command-Q'},
            exec: function(editor) {
                if( editor.getSession().getUndoManager().hasUndo() == true )
                confirm("exit?", function() { hide("editorwin"); });
                else hide("editorwin");
            },
            readOnly: true
          });

        },
        function(text) {
          alert('error:' + text);
          hide("editorwin");
        });
      };

      // get stacktrace from server
      var trace = function(id,json) {
          if( getDivContent(id).length == 0 ) {
            updateDivContent(id, "<xmp>Connecting...</xmp>");
            geturl("/stacktrace.text?p=" + json, function(text) {

              var html = '', arr = text.split("\n");
              for(i = 0; i < arr.length; i++) {
                html = html + '<li>' + arr[i] + '</li>';
              }
              html = '<ol>' + html + '</ol>';
//              updateDivContent("editor", html);
//              toggle("editor");

              updateDivContent(id, html); //"<xmp>"+text+"</xmp>");
            },
            function(text) {
              updateDivContent(id, "<xmp>ajax error :(</xmp>");
            });
          }
          toggle(id);
      };

      /* Environment variable view */
      var envPath = "";
      var refreshEnv = function() {
        geturl("/env.json?p=" + envPath, function(text) {
          var json = eval("(" + text + ")");

          /* Header */
          var html = "<a href='#' onclick=\"setEnvPath('')\">env</a>";
          var acc = "";
          var p = json.path != "" ? json.path.split(".") : [];
          for (var i = 0; i < p.length; i++) {
            acc += "." + p[i];
            html += " <a href='#' onclick=\"setEnvPath('" + acc + "')\">" +
                    truncate(p[i], 10) + "</a>";
          };
          updateDivContent("envheader", html);

          /* Handle invalid table path */
          if (!json.valid) {
            updateDivContent("envvars", "Bad path");
            return;
          };

          /* Variables */
          var html = "<table>";
          for (var i = 0; json.vars[i]; i++) {
            var x = json.vars[i];
            var fullpath = (json.path + "." + x.key).replace(/^\\./, "");
            var k = truncate(x.key, 15);
            if (x.type == "table") {
              k = "<a href='#' onclick=\"setEnvPath('" + fullpath + "')\">" +
                  k + "</a>";
            };
            var v = "<a href='#' onclick=\"insertVar('" +
                    fullpath.replace(/\\.(-?[0-9]+)/g, "[$1]") +
                    "');\">" + x.value + "</a>";
            html += "<tr><td>" + k + "</td><td>" + v + "</td></tr>";
          };
          html += "</table>";
          updateDivContent("envvars", html);
        });
      };
      var setEnvPath = function(p) {
        envPath = p;
        refreshEnv();
      };
      var insertVar = function(p) {
        var b = document.getElementById("inputbox");
        b.value += p;
        b.focus();
      };
      setInterval(refreshEnv, {TIMEOUT} * 1000);
    </script>

<script>
      var editor = ace.edit("editor");
          editor.setTheme("ace/theme/monokai");
          editor.getSession().setMode("ace/mode/c_cpp");
</script>

  </body>
</html>
);
