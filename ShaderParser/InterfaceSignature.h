// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../Core/Prefix.h"
#include "../Core/Exceptions.h"
#include "../Utility/Mixins.h"
#include <vector>

namespace ShaderSourceParser
{
    class FunctionSignature : noncopyable
    {
    public:
        std::string     _returnType;
        std::string     _name;

        class Parameter
        {
        public:
            enum Direction { In = 1<<0, Out = 1<<1 };
            std::string _type;
            std::string _semantic;
            std::string _name;
            unsigned _direction;
        };

        std::vector<Parameter> _parameters;

        FunctionSignature();
        ~FunctionSignature();
        FunctionSignature(FunctionSignature&& moveFrom);
        FunctionSignature& operator=(FunctionSignature&& moveFrom) never_throws;
    };

    class ParameterStructSignature : noncopyable
    {
    public:
        class Parameter
        {
        public:
            std::string _type;
            std::string _semantic;
            std::string _name;
        };

        std::string _name;
        std::vector<Parameter> _parameters;

        ParameterStructSignature();
        ~ParameterStructSignature();
        ParameterStructSignature(ParameterStructSignature&& moveFrom);
        ParameterStructSignature& operator=(ParameterStructSignature&& moveFrom) never_throws;
    };

    class ShaderFragmentSignature : noncopyable
    {
    public:
        std::vector<FunctionSignature>          _functions;
        std::vector<ParameterStructSignature>   _parameterStructs;

        ShaderFragmentSignature();
        ~ShaderFragmentSignature();
        ShaderFragmentSignature(ShaderFragmentSignature&& moveFrom);
        ShaderFragmentSignature& operator=(ShaderFragmentSignature&& moveFrom) never_throws;
    };


    ShaderFragmentSignature     BuildShaderFragmentSignature(const char sourceCode[], size_t sourceCodeLength);

    namespace Exceptions
    {
        class CompileError : public ::Exceptions::BasicLabel
        {
        public:
            CompileError(const char label[]) never_throws;
        };
    }
}

