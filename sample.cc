#include <iostream>
#include <string>
#include <stdio.h>
#include "WebDebugMenu.h"

#include <xmmintrin.h>
#include <stdio.h>
#include <memory.h>
#include <map>
#include <wchar.h>

//#define wdmDisable
#include "WebDebugMenu.h"

#include "solace.hpp"

#ifdef _WIN32
#   include <windows.h>
#else
#   include <unistd.h>
#endif

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


#ifdef _WIN32
#define sleep(ms) Sleep(ms)
#else
#define sleep(ms) usleep((ms)*1000)
#endif

struct teststruct {
    int int_value;
    float float_value;

    teststruct() : int_value(10), float_value(10.0f) {}
};

class Test
{
public:
    Test()
        : m_i32(1)
        , m_ci32(64)
        , m_b(true)
        , m_f32(10.0f)
    {
        memset(&m_i32a, 0, sizeof(m_i32a));
        strcpy(m_charstr, "test charstr");
        wcscpy(m_wcharstr, L"test wcharstr");

        m_m128 = _mm_set_ps(0.0f, 1.0f, 2.0f, 3.0f);
        wdmScope( wdmString p = wdmFormat("Test0x%p", this); );
        wdmAddNode(p+"/m_i32", &m_i32, wdmMakeRange(100, 500));
        wdmAddNode(p+"/m_i32a", &m_i32a, wdmMakeRange(0, 100));
  //      wdmAddNode(p+"/property_i32", this, &Test::getI32, &Test::setI32, wdmMakeRange(0, 500));
        wdmAddNode(p+"/property_ro_i32", this, &Test::getI32);
        wdmAddNode(p+"/m_ci32", &m_ci32);
        wdmAddNode(p+"/m_b", &m_b);
        wdmAddNode(p+"/m_f32", &m_f32, wdmMakeRange(- 1.0f, 1.0f));
        wdmAddNode(p+"/m_m128", &m_m128, wdmMakeRange(0.0f, 10.0f));
        wdmAddNode(p+"/dir2/m_charstr", &m_charstr);
        wdmAddNode(p+"/dir2/m_wcharstr", &m_wcharstr);
        wdmAddNode(p+"/dir2/dir3/property_charstr", this, &Test::getCharStr);
        wdmAddNode(p+"/dir2/print()", &Test::print, this);
       // wdmAddNode(p+"/func1()", &Test::func1, this);
       // wdmAddNode(p+"/func2()", &Test::func2, this);

        m_str = "1234";
        m_str2 = "123412321893218932189323892189329388921";

        m_testmap[123] = "hello";
        m_testmap[456] = "world";
    }

    ~Test()
    {
        wdmEraseNode( wdmFormat("Test0x%p", this) );
    }

    void setI32(const int &v) { m_i32=v; }
    const int& getI32() const { return m_i32; }
    const char* getCharStr() const { return m_charstr; }

    void func1(int a) { printf("func1: %d\n", a); }
    void func2(int a, int b) { printf("func2: %d %d\n", a, b); }

    void print() const
    {
        printf(
            "m_i32:%d\n"
            "m_ci32:%d\n"
            "m_b:%d\n"
            "m_f32:%f\n",
            m_i32, m_ci32, m_b, m_f32 );
    }

private:
    int m_i32;
    std::int64_t m_i32a[4];
    const int m_ci32;
    bool m_b;
    float m_f32;
    __m128 m_m128;
    char m_charstr[16];
    wchar_t m_wcharstr[16];
    teststruct m_struct[2];
    std::string m_str, m_str2;
    std::map<int, std::string> m_testmap;
};

#if 0

class Test2
{
public:
    Test2() : m_i32(1) , m_b(true)
    {
        std::fill_n(m_f32x4, _countof(m_f32x4), 1.0f);
        m_m128 = _mm_set_ps(0.0f, 1.0f, 2.0f, 3.0f);
        sprintf(m_charstr, "test charstr");
        for(int i=0; i<_countof(m_pair); ++i) {
            m_pair[i].first=i+1; m_pair[i].second=(i+1)*2.0f;
        }

        wdmScope( wdmString p = wdmFormat("Test2[%p]", this); );
        wdmAddNode(p+"m_i32", &m_i32);
        wdmAddNode(p+"m_f32x4", &m_f32x4[0]);
        wdmAddNode(p+"m_b", &m_b);
        wdmAddNode(p+"m_m128", &m_m128);
        wdmAddNode(p+"m_charstr", m_charstr);
        wdmAddNode(p+"m_pair", &m_pair[0]);
    }

    ~Test2()
    {
        wdmEraseNode( wdmFormat("Test2[%p]", this) );
    }

private:
    int m_i32;
    float m_f32x4[4];
    bool m_b;
    __m128 m_m128;
    char m_charstr[16];
    std::pair<int, float> m_pair[2];
};

#else

typedef Test Test2;

#endif

#include <clocale>

    struct threaded {
        void operator()() {
            setlocale(LC_ALL, "utf-8");
            wdmInitialize();
            //wdmOpenBrowser();
            {
                Test a,b;
                Test2 c;
                bool end_flag = false;
                wdmAddNode("end_flag", &end_flag);
                while(!end_flag) {
                    wdmFlush();
                    sleep(100);
                }
            }
            wdmFinalize();
        }
    };

    std::map<std::string,std::string> get_keys( const std::string &prefix_ ) {
		std::string prefix = prefix_;
		if( prefix == "." ) prefix.clear();

		while( prefix.size() && prefix[0] == '.' ) prefix = prefix.substr(1);
		while( prefix.size() && prefix.back() == '.' ) prefix = prefix.substr( 0, prefix.size() - 1 );

        std::map< std::string, std::string > map, out;
        map[ "random_text" ] = std::string() + "\"hi " + char( rand() % 255 ) + "!\"";
        map[ "hello" ] = "\"world\"";
        map[ "abc" ] = "";
        map[ "abc.def" ] = "123";

        for( std::map< std::string, std::string >::const_iterator it = map.begin(), end = map.end(); it != end; ++it ) {
            const std::string &key = it->first;
            const std::string &value = it->second;
            std::string right = key.substr( prefix.size() );
            if( GetAsyncKeyState(VK_F9) & 0x8000 ) __asm int 3;
            if( key.substr( 0, prefix.size() ) == prefix ) {
                bool has_subkey = right.size() && (right.find_first_of('.') != std::string::npos);
				if( !has_subkey ) out[ std::string() + "." + right ] = value;
            }
        }

        for( auto &kv : out ) {
            std::cout << prefix << "\t" << kv.first << "\t" << kv.second << "\n";
        }

        return out;
    }


int main() {

    std::thread(threaded()).detach();

    // 
    std::vector<std::string> highlights;
    highlights.push_back( "lvl0" );
    highlights.push_back( "lvl1" );
    highlights.push_back( "lvl2" );
    highlights.push_back( "lvl3" );
    highlights.push_back( "err" );
    highlights.push_back( "warn" );
    highlights.push_back( "log" );
    highlights.push_back( "info" );
    highlights.push_back( "mesh" );
    highlights.push_back( "texture" );
    highlights.push_back( "asset" );
    highlights.push_back( "load" );
    highlights.push_back( "save" );
    highlights.push_back( "init" );
    highlights.push_back( "jpg" );
    highlights.push_back( "debug" );
    highlights.push_back( "release" );
    solace::set_highlights( highlights );

	// install solace html server
    solace::webinstall( 8080, &evaluate, &get_keys );
    //solace::webopen();

    // write to our log
    solace::cout << "Succesfully installed! " << 123 << std::endl;

    // capture std::cout and log it from here
    // solace::capture( std::cout );
    solace::capture( 1 );

    for( int i = 0; i < 500; ++i ) {
        printf("Succesfully captured! lvl%d!\n", i );
        //std::cout << "Succesfully captured! lvl" << i << "!" << std::endl;
        //solace::cout << "Succesfully captured! lvl" << i << "!" << std::endl;
    }

    std::cout << "https://raw.githubusercontent.com/r-lyeh/dot/master/images/lenna3.jpg" << std::endl;

    // do whatever
    for( std::string s; std::getline(std::cin, s); )
    {}

    // restore std::cout stream
    solace::release( std::cout );
}

namespace boost {
    void throw_exception( const std::exception & ) {
    }
}


