// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#include "Console.h"
#include "../Utility/PtrUtils.h"
#include "../../Utility/Mixins.h"
#include "../../Utility/StringFormat.h"
#include "../../Utility/StringUtils.h"
#include "../../Utility/MemoryUtils.h"
#include "../../Core/Exceptions.h"
#include "../../Math/Vector.h"
#include <iterator>
#include <algorithm>

#undef new

    #include <lua.hpp>
    #include <LuaBridge.h>
    #include <RefCountedPtr.h>

#if defined(DEBUG_NEW)
    #define new DEBUG_NEW
#endif

namespace ConsoleRig
{

    class LuaState
    {
    public:
        lua_State* L;
        operator lua_State*() { return L; }

        int             PCall(int argumentCount, int returnValueCount);
    
        LuaState();
        ~LuaState();

    private:
        static void*    AllocationBridge(void *userData, void *ptr, size_t osize, size_t nsize);
        static int      PanicBridge(lua_State* L);
        static int      ErrorHandler(lua_State* L);
        static void*    GetTracebackKey();
        static int      Print(lua_State* L);
    };





            //////   C O R E   C O N S O L E   B E H A V I O U R   //////

    static std::unique_ptr<LuaState>    g_ConsoleLUA = nullptr;
    Console*                            Console::s_instance = nullptr;

    void            Console::Execute(const std::string& str)
    {
        Print("{Color:af3f7f}Executing string -- {Color:7F7F7F}" + str + "\n");

        lua_State* L = *g_ConsoleLUA;
        luaL_loadstring(L, str.c_str());
        int errorCode = g_ConsoleLUA->PCall(0, 0);
        if (errorCode != LUA_OK) {
            const char* msg = lua_tostring(L, -1);
            if (msg) {
                Print(msg);
            }
            lua_pop(L, 1);
        }
    }

    std::vector<std::string>    CollectAutoCompleteList(lua_State*L, const std::string& input, size_t iterateStart)
    {
        std::vector<std::string> result;
        size_t compareLength = input.size() - iterateStart;
        if (compareLength) {
            DEBUG_ONLY(int stackSizeStart2 = lua_gettop(L));
            assert(lua_istable(L, -1));
            lua_pushnil(L);
            while (lua_next(L, -2) != 0) {

                size_t length = 0;
                const char* name = lua_tolstring(L, -2, &length);
                if (name) {
                    if (!XlCompareString(name, "__propget")) {

                            //
                            //      The fields of "__propget" are just like imaginary
                            //      members of this table
                            //
                        lua_pushvalue(L, -3);
                        lua_getfield(L, -1, name);
                        auto newResult = CollectAutoCompleteList(L, input, iterateStart);
                        result.insert(result.end(), newResult.begin(), newResult.end());
                        lua_pop(L, 2);

                    } else {
                        if (length >= compareLength) {
                            if (!XlComparePrefixI(name, &input[iterateStart], compareLength)) {
                                result.push_back(input.substr(0, iterateStart) + name);
                            }
                        }
                    }
                }

                lua_pop(L, 1);
            }
            // lua_pop(L, 1);  DavidJ -- seems to be an automatic pop in lua_next() when it returns 0

            DEBUG_ONLY(int stackSizeEnd2 = lua_gettop(L));
            assert(stackSizeEnd2 == stackSizeStart2);
        }

        return std::move(result);
    }

    std::vector<std::string>    Console::AutoComplete(const std::string& input)
    {
        lua_State* L = *g_ConsoleLUA;

            //
            //      Separate the input string into parts with "." or ":"
            //      these are tables, etc, we should look up
            //
        DEBUG_ONLY(int stackSizeStart2 = lua_gettop(L));
        lua_getglobal(L, "_G");
        int tablesPushed = 1;
        std::string::size_type iterateStart = 0;
        for (;;) {
            std::string::size_type nextPart = input.find_first_of(".:", iterateStart);
            if (nextPart == std::string::npos) {
                break;
            }

            std::string::size_type iterateEnd = nextPart;
            std::string table = input.substr(iterateStart, iterateEnd-iterateStart);

                //      (meta-method version here...)
            lua_getfield(L, -1, table.c_str());
            ++tablesPushed;

            if (lua_isnil(L, -1)) {
                    // pushed a bad table name. Nothing here.
                lua_pop(L, tablesPushed);
                assert(lua_gettop(L) == stackSizeStart2);
                return std::vector<std::string>();
            }

                // if the object is not a table itself, it might have a "meta-table"
            if (!lua_istable(L, -1)) {
                lua_getmetatable(L, -1);
                lua_remove(L, -2);
                if (lua_isnil(L, -1) || !lua_istable(L, -1)) {
                    lua_pop(L, tablesPushed);
                    assert(lua_gettop(L) == stackSizeStart2);
                    return std::vector<std::string>();
                }
            }

            iterateStart = nextPart+1;
        }

        auto result = CollectAutoCompleteList(L, input, iterateStart);
        lua_pop(L, tablesPushed);
        assert(lua_gettop(L) == stackSizeStart2);
        return result;
    }

    static std::u16string      AsUTF16(const std::string& input)
    {
        char16_t buffer[1024];
        utf8_2_ucs2((utf8*)AsPointer(input.begin()), input.size(), buffer, dimof(buffer));
        return std::u16string(buffer);
    }

    static std::u16string      AsUTF16(const char input[])
    {
        char16_t buffer[1024];
        utf8_2_ucs2((utf8*)input, XlStringLen(input), buffer, dimof(buffer));
        return std::u16string(buffer);
    }

    void            Console::Print(const std::string& message)
    {
        if (!this) return;  // hack!
        Print(AsUTF16(message));
    }

    void Console::Print(const char message[])
    {
        if (!this) return;  // hack!
        Print(AsUTF16(message));
    }

    void            Console::Print(const std::u16string& message)
    {
        if (!this) return;  // hack!
        std::u16string::size_type currentOffset = 0;
        std::u16string::size_type stringLength = message.size();
        bool lastLineComplete = _lastLineComplete;

        while (currentOffset < stringLength) {
            const std::u16string::size_type start = currentOffset;
            const std::u16string::size_type s     = message.find_first_of((char16_t*)L"\r\n", currentOffset);
            std::u16string::size_type end;
            bool completeLine = false;

            if (s != std::string::npos) {
                end = s;
                completeLine = true;
            } else {
                end = message.size();
            }

            if (end > start) {
                if (!lastLineComplete && !_lines.empty()) {
                    _lines[_lines.size()-1] += message.substr(start, end-start);
                } else {
                    _lines.push_back(message.substr(start, end-start));
                }

                lastLineComplete = completeLine;
            }

            currentOffset = end;
            while (currentOffset < stringLength && (message[currentOffset]=='\r'||message[currentOffset]=='\n')) {
                ++currentOffset;
            }
        }

        _lastLineComplete = lastLineComplete;
    }

    std::vector<std::u16string>    Console::GetLines(unsigned lineCount, unsigned scrollback)
    {
        std::vector<std::u16string> result;
        signed linesToGet = std::max(0, std::min(signed(lineCount), signed(_lines.size())-signed(scrollback)));
        result.reserve(linesToGet);

        if (linesToGet <= 0) {
            return result;
        }

        std::copy(
            _lines.end() - scrollback - linesToGet, _lines.end() - scrollback,
            std::back_inserter(result));

        return result;
    }

    unsigned Console::GetLineCount() const
    {
        return unsigned(_lines.size());
    }

    lua_State*  Console::GetLuaState()
    {
        return (lua_State*)*g_ConsoleLUA;
    }

    Console::Console()  
    {
        _lastLineComplete = false;
        _lines.push_back(std::u16string());

        assert(!s_instance);
        s_instance = this;

        assert(!g_ConsoleLUA);
        g_ConsoleLUA = std::make_unique<LuaState>();
    }

    Console::~Console() 
    {
        g_ConsoleLUA.reset();

        assert(s_instance==this);
        s_instance = nullptr;
    }





            //////   B A S I C   L U A   B E H A V I O U R   //////

    void * LuaState::AllocationBridge(void *userData, void *ptr, size_t osize, size_t nsize)
    {
        (void)userData;  (void)osize;  /* not used */
        if (nsize == 0) {
            free(ptr);
            return NULL;
        } else {
            return realloc(ptr, nsize);
        }
    }

    #pragma warning(disable:4702)   //  warning C4702: unreachable code

    int LuaState::PanicBridge(lua_State* L)
    {
        ThrowException(std::exception());
        return 0;
    }

    int LuaState::ErrorHandler(lua_State* L)
    {
        lua_Debug ar;
        XlZeroMemory(ar);

        const char* sErr = lua_tostring(L, 1);
        if (sErr) {
            Console::GetInstance().Print(std::string("Lua Error: ") + sErr + "\n");
        }

        /*
            DavidJ -- this is causing stack corruption...?
        int stackIndex = 0;
        while (lua_getstack(L, stackIndex++, &ar)) {
            lua_getinfo(L, "lnS", &ar);
            Console::GetInstance().Print(XlDynFormatString("  [%i] %s, (%s: %d)\n", stackIndex-1, ar.name?ar.name:"<null>", ar.source?ar.source:ar.short_src, ar.currentline));
        }
        */

        return 0;
    }

    int LuaState::Print(lua_State *L)
    {
        // LuaState* const luaState = static_cast <LuaState*> (
        //     lua_touserdata (L, lua_upvalueindex (1)));

        std::string text;
        int n = lua_gettop(L);  /* number of arguments */
        lua_getglobal(L, "tostring");
        for (int i=1; i<=n; i++) {
            const char *s;
            size_t l;
            lua_pushvalue(L, -1);  /* function to be called */
            lua_pushvalue(L, i);   /* value to print */
            lua_call(L, 1, 1);
            s = lua_tolstring(L, -1, &l);  /* get result */
            if (s == NULL) {
                return luaL_error(L,
                    LUA_QL("tostring") " must return a string to " LUA_QL("print"));
            }

            if (i>1) {
                text += " ";
            }

            text += std::string(s, l);
            lua_pop(L, 1);  /* pop result */
        }

        Console::GetInstance().Print(text);
        return 0;
    }

    static char addressPlacementHolder;
    void* LuaState::GetTracebackKey() { return &addressPlacementHolder; }

    int LuaState::PCall(int argumentCount, int returnValueCount)
    {
            //
            //      Get the error handler function
            //          push LUA_REGISTRYINDEX[getTracebackKey()]
        lua_pushlightuserdata(L, GetTracebackKey());
        lua_rawget(L, LUA_REGISTRYINDEX);

            //  
            //      Move the error handle function to before the parameters 
            //      on the stack
            //
        int errorHandlerStackIndex = -(argumentCount+2);
        lua_insert(L, errorHandlerStackIndex);

        int result = lua_pcall(L, argumentCount, returnValueCount, errorHandlerStackIndex);
        if (result == 0) {
            lua_remove (L, -(returnValueCount+1));
        } else {
            lua_remove (L, -2);
        }
        return result;
    }

    LuaState::LuaState()
    {
        L = lua_newstate(&AllocationBridge, nullptr);
        luaL_openlibs(L);
        lua_atpanic(L, &PanicBridge);

        lua_pushlightuserdata(L, this);
        lua_pushcclosure(L, &LuaState::Print, 1);
        lua_setglobal(L, "print");

            //
            //      Store a pointer to the error handler function
            //      in the lua registry at the key "getTracebackKey()"
            //          LUA_REGISTRYINDEX[getTracebackKey()] = &ErrorHandler
            //
        lua_pushlightuserdata(L, GetTracebackKey());
        lua_pushcclosure(L, &ErrorHandler, 0);
        lua_rawset(L, LUA_REGISTRYINDEX);


            //
            //      We need to create the "cv" namespace, to have something 
            //      to put cvar property methods into
            //
        luabridge::getGlobalNamespace(L).beginNamespace("cv").endNamespace();
    }

    LuaState::~LuaState()
    {
        lua_close(L);
    }

    namespace Detail
    {
        using namespace luabridge;

        template <typename MemFn, typename D=MemFn> struct ImmMemberFunction {};

        template <typename R, typename E, typename D>
            struct ImmMemberFunction <R (*) (E), D>
        {
            typedef None Params;
            static R call (D fp, E e, TypeListValues<Params>)            { return fp(e); }
        };

        template <typename R, typename P1, typename E, typename D>
            struct ImmMemberFunction <R (*) (E, P1), D>
        {
            typedef TypeList <P1> Params;
            static R call (D fp, E e, TypeListValues<Params> tvl)       { return fp(e, tvl.hd); }
        };

        template <typename R, typename P1, typename P2, typename E, typename D>
            struct ImmMemberFunction <R (*) (E, P1, P2), D>
        {
            typedef TypeList <P1, P2> Params;
            static R call (D fp, E e, TypeListValues<Params> tvl)       { return fp(e, tvl.hd, tvl.tl.hd); }
        };

        template <typename R, typename P1, typename P2, typename P3, typename E, typename D>
            struct ImmMemberFunction <R (*) (E, P1, P2, P3), D>
        {
            typedef TypeList <P1, TypeList <P2, TypeList <P3> > > Params;
            static R call (D fp, E e, TypeListValues<Params> tvl)            { return fp(e, tvl.hd, tvl.tl.hd, tvl.tl.tl.hd); }
        };

        template <typename Type>
            static Type ConsoleVariable_Getter(ConsoleVariable<Type>* attachedValue)
        {
            return (*attachedValue->_attachedValue);
        }

        template <typename Type>
            static Type ConsoleVariable_Setter(ConsoleVariable<Type>* attachedValue, Type newValue)
        {
            (*attachedValue->_attachedValue) = newValue;
            return *attachedValue->_attachedValue;
        }


        template <typename Type>
            std::vector<std::unique_ptr<std::pair<Type, ConsoleVariable<Type>>>>&  GetConsoleVariableTable()
        {
            static std::vector<std::unique_ptr<std::pair<Type, ConsoleVariable<Type>>>> staticTable;
            return staticTable;
        }

        template <typename Type>
            class CompareConsoleVariable 
        {
        public:
            typedef std::pair<Type, ConsoleVariable<Type>> Pair;
            bool operator()(const char lhs[], const std::unique_ptr<Pair>& rhs) const      { return XlCompareString(lhs, rhs->second.Name().c_str()) < 0; }
            bool operator()(const std::unique_ptr<Pair>& lhs, const char rhs[]) const      { return XlCompareString(lhs->second.Name().c_str(), rhs) < 0; }
        };

        #undef new

            template <typename Type>
                Type&       FindTweakable(const char name[], Type defaultValue)
            {
                auto& table  = GetConsoleVariableTable<Type>();
                auto i       = std::lower_bound(table.cbegin(), table.cend(), name, CompareConsoleVariable<Type>());
                if (i!=table.cend() && !XlCompareString((*i)->second.Name().c_str(), name)) {
                    return (*i)->first;
                }

                typedef std::pair<Type, ConsoleVariable<Type>> Pair;
                std::unique_ptr<Pair> p(new Pair(defaultValue, ConsoleVariable<Type>()));
                Type& result = std::get<0>(*p);
                ConsoleVariable<Type>& var = std::get<1>(*p);
                var.~ConsoleVariable<Type>();
                new(&var) ConsoleVariable<Type>(name, result);
                table.insert(i, std::move(p));
                return result;
            }

        #if defined(DEBUG_NEW)
            #define new DEBUG_NEW
        #endif

        template int&           FindTweakable<int>(const char name[], int defaultValue);
        template float&         FindTweakable<float>(const char name[], float defaultValue);
        template std::string&   FindTweakable<std::string>(const char name[], std::string defaultValue);
        template bool&          FindTweakable<bool>(const char name[], bool defaultValue);
        template Float3&        FindTweakable<Float3>(const char name[], Float3 defaultValue);
        template Float4&        FindTweakable<Float4>(const char name[], Float4 defaultValue);

        template<   class Type,
                    class MemFn,
                    class ReturnType = typename luabridge::FuncTraits<MemFn>::ReturnType>
        struct ConsoleVariable_CallFunction
        {
            typedef ConsoleVariable<Type>                         T;
            typedef typename ImmMemberFunction<MemFn>::Params     Params;
            static int Call(lua_State* L)
            {
                using namespace luabridge;
                assert (lua_isuserdata (L, lua_upvalueindex(1)));
                T*t = (T*)lua_touserdata(L, lua_upvalueindex(1));

                assert (lua_isuserdata (L, lua_upvalueindex (2)));
                MemFn fp = static_cast<MemFn>(lua_touserdata(L, lua_upvalueindex (2)));

                assert (fp != 0);
                ArgList<Params> args (L);
                Stack<ReturnType>::push(L, ImmMemberFunction<MemFn>::call(fp, t, args));
                return 1;
            }
        };
    }




            //////   C O N S O L E   V A R I A B L E   H E L P E R   //////

    template <typename Type> class ConsoleVariable : noncopyable
    {
    public:
        ConsoleVariable(const std::string& name, Type& attachedValue, const char cvarNamespace[] = nullptr);
        ConsoleVariable();
        ~ConsoleVariable();

        ConsoleVariable(ConsoleVariable&& moveFrom);
        ConsoleVariable& operator=(ConsoleVariable&& moveFrom);

        const std::string& Name() const { return _name; }

        Type*           _attachedValue;
    private:
        std::string     _name;
        std::string     _cvarNamespace;
    };

    template <typename Type>
        ConsoleVariable<Type>::ConsoleVariable(const std::string& name, Type& attachedValue, const char cvarNamespace[])
    :   _name(name)
    ,   _attachedValue(&attachedValue)
    {
            //
            //          Register the variable as a global value in LUA
            //
        lua_State* L = *g_ConsoleLUA.get();        // (use the global lua state for console variables)

        using namespace luabridge;

        auto get = &Detail::ConsoleVariable_Getter<Type>;
        auto set = &Detail::ConsoleVariable_Setter<Type>;

        lua_getglobal(L, "_G");
        rawgetfield(L, -1, cvarNamespace?cvarNamespace:"cv");
        assert(lua_istable (L, -1));

            // Get
        rawgetfield (L, -1, "__propget");
        assert (lua_istable (L, -1));
        lua_pushlightuserdata(L, this);
        lua_pushlightuserdata(L, get);
        lua_pushcclosure(L, &Detail::ConsoleVariable_CallFunction<Type, decltype(get)>::Call, 2);
        rawsetfield(L, -2, name.c_str());
        lua_pop(L, 1);

            // Set
        rawgetfield(L, -1, "__propset");
        assert(lua_istable(L, -1));
        lua_pushlightuserdata(L, this);
        lua_pushlightuserdata(L, set);
        lua_pushcclosure(L, &Detail::ConsoleVariable_CallFunction<Type, decltype(set)>::Call, 2);
        rawsetfield(L, -2, name.c_str());
        lua_pop(L, 1);

        lua_pop(L, 2);      // pop _G & cv namespace
    }

    template <typename Type>
        ConsoleVariable<Type>::ConsoleVariable() {}

    template <typename Type>
        ConsoleVariable<Type>::~ConsoleVariable()
    {
        if (!_name.empty() && g_ConsoleLUA) {
            lua_State* L = *g_ConsoleLUA;

            lua_getglobal(L, "_G");
            luabridge::rawgetfield(L, -1, _cvarNamespace.empty()?"cv":_cvarNamespace.c_str());
            assert(lua_istable (L, -1));

            luabridge::rawgetfield(L, -1, "__propget");
            lua_pushnil(L);
            luabridge::rawsetfield(L, -2, _name.c_str());
            lua_pop(L, 1);

            luabridge::rawgetfield(L, -1, "__propset");
            lua_pushnil(L);
            luabridge::rawsetfield(L, -2, _name.c_str());
            lua_pop(L, 1);

            lua_pop(L, 2);      // pop _G & cv namespace
        }
    }

    template <typename Type>
        ConsoleVariable<Type>::ConsoleVariable(ConsoleVariable&& moveFrom)
    :       _name(std::move(moveFrom._name))
    ,       _cvarNamespace(std::move(moveFrom._cvarNamespace))
    ,       _attachedValue(std::move(moveFrom._attachedValue))
    {
    }

    template <typename Type>
        ConsoleVariable<Type>& ConsoleVariable<Type>::operator=(ConsoleVariable<Type>&& moveFrom)
    {
        _name           = std::move(moveFrom._name);
        _cvarNamespace  = std::move(moveFrom._cvarNamespace);
        _attachedValue  = std::move(moveFrom._attachedValue);
        return moveFrom;
    }


    template ConsoleVariable<int>;
    template ConsoleVariable<float>;
    template ConsoleVariable<std::string>;
    template ConsoleVariable<bool>;
    template ConsoleVariable<Float3>;
    template ConsoleVariable<Float4>;



}


