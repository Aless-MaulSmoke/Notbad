// Testes do src/langcore.h (nucleo puro do sistema de idiomas).
// Nao faz parte do projeto do Visual Studio. Para rodar (Linux/macOS/MinGW):
//
//   g++ -std=c++14 -Wall -Wextra -pedantic -I src tools/test_langcore.cpp -o test_langcore
//   ./test_langcore lang/en.json
//
// O argumento (opcional) e um arquivo JSON real para tambem ser lido.
// No Windows wchar_t tem 16 bits; aqui o texto e testado como char16_t (igual
// ao wchar_t do Windows), char32_t e wchar_t do sistema de teste.

#include "langcore.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::printf("FALHOU %s:%d: %s\n", __FILE__, __LINE__, #cond);        \
        }                                                                        \
    } while (0)

// Texto ASCII -> S.
template <class S>
static S A(const char* s)
{
    S out;
    while (*s != '\0')
        out.push_back((typename S::value_type)(unsigned char)*s++);
    return out;
}

// Code points -> S (usa a conversao de UTF-16 quando o tipo tem 16 bits).
template <class S>
static S CP(std::initializer_list<uint32_t> cps)
{
    S out;
    for (uint32_t cp : cps)
        langcore::AppendCodePoint(out, cp);
    return out;
}

template <class S>
static bool Parse(const std::string& json, std::unordered_map<S, S>& out)
{
    return langcore::ParseFlatJson<S>(json.data(), json.size(), out);
}

template <class S>
static void TestParser()
{
    typedef std::unordered_map<S, S> Map;
    Map m;

    // basico
    CHECK(Parse<S>("{\"a\":\"b\",\"c\":\"d\"}", m));
    CHECK(m.size() == 2 && m[A<S>("a")] == A<S>("b") && m[A<S>("c")] == A<S>("d"));

    // objeto vazio, espacos, virgula sobrando, chave repetida (vale a ultima)
    CHECK(Parse<S>("  { }  ", m) && m.empty());
    CHECK(Parse<S>("{ \"a\" : \"1\" ,\n\t\"a\":\"2\", }", m) && m.size() == 1 && m[A<S>("a")] == A<S>("2"));

    // BOM UTF-8
    CHECK(Parse<S>("\xEF\xBB\xBF{\"k\":\"v\"}", m) && m.size() == 1);

    // metadados "_" e chave vazia ficam de fora
    CHECK(Parse<S>("{\"_info\":\"x\",\"\":\"y\",\"k\":\"v\"}", m) && m.size() == 1 && m.count(A<S>("k")) == 1);

    // escapes simples
    CHECK(Parse<S>("{\"k\":\"a\\tb\\nc\\\"d\\\\e\\/f\\b\\f\\r\"}", m));
    CHECK(m[A<S>("k")] == CP<S>({'a', 9, 'b', 10, 'c', '"', 'd', '\\', 'e', '/', 'f', 8, 12, 13}));

    // \u: BMP, maiusculas/minusculas, e par substituto (emoji U+1F600)
    CHECK(Parse<S>("{\"k\":\"\\u00e7\\u00C3o\"}", m) && m[A<S>("k")] == CP<S>({0xE7, 0xC3, 'o'}));
    CHECK(Parse<S>("{\"k\":\"\\uD83D\\uDE00\"}", m) && m[A<S>("k")] == CP<S>({0x1F600}));

    // substitutos soltos viram U+FFFD sem derrubar o arquivo
    CHECK(Parse<S>("{\"k\":\"\\uD83Dx\"}", m) && m[A<S>("k")] == CP<S>({0xFFFD, 'x'}));
    CHECK(Parse<S>("{\"k\":\"\\uDE00\"}", m) && m[A<S>("k")] == CP<S>({0xFFFD}));
    CHECK(Parse<S>("{\"k\":\"\\uD83D\\u0041\"}", m) && m[A<S>("k")] == CP<S>({0xFFFD, 'A'}));

    // UTF-8 direto: 2, 3 e 4 bytes
    CHECK(Parse<S>("{\"k\":\"a\xC3\xA7\xC3\xA3o \xE2\x82\xAC \xF0\x9F\x98\x80\"}", m));
    CHECK(m[A<S>("k")] == CP<S>({'a', 0xE7, 0xE3, 'o', ' ', 0x20AC, ' ', 0x1F600}));

    // chave tambem aceita escapes/UTF-8
    CHECK(Parse<S>("{\"\xC3\xA7\":\"v\"}", m) && m.count(CP<S>({0xE7})) == 1);

    // erros: o mapa de saida precisa ficar vazio
    const char* bad[] = {
        "",                                // vazio
        "[]",                              // nao e objeto
        "{",                               // nao terminado
        "{\"a\"}",                         // sem valor
        "{\"a\":1}",                       // valor nao e texto
        "{\"a\":null}",
        "{\"a\":{\"b\":\"c\"}}",           // aninhado
        "{\"a\":\"b\"",                    // sem }
        "{\"a\":\"b\"} x",                 // lixo depois
        "{\"a\":\"b\" \"c\":\"d\"}",       // sem virgula
        "{a:\"b\"}",                       // chave sem aspas
        "{\"a\":\"b",                      // texto nao terminado
        "{\"a\":\"\\q\"}",                 // escape invalido
        "{\"a\":\"\\u12\"}",               // \u incompleto
        "{\"a\":\"\\u12G4\"}",             // hex invalido
        "{\"a\":\"\xC3\"}",                // UTF-8 truncado
        "{\"a\":\"\xC0\x80\"}",            // UTF-8 curto demais (overlong)
        "{\"a\":\"\xED\xA0\x80\"}",        // substituto em UTF-8
        "{\"a\":\"\xF5\x80\x80\x80\"}",    // acima de U+10FFFF
        "{\"a\":\"\xFF\"}",                // byte invalido
    };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i)
    {
        m[A<S>("lixo")] = A<S>("lixo");
        CHECK(!Parse<S>(bad[i], m));
        CHECK(m.empty());
    }
    {
        const char* err = NULL;
        CHECK(!langcore::ParseFlatJson<S>("{\"a\":1}", 7, m, &err) && err != NULL && err[0] != '\0');
    }
}

template <class S>
static void TestFormatSpecs()
{
    std::string specs;

    CHECK(langcore::ExtractFormatSpecs(A<S>("Ln %u, Col %u"), &specs) && specs == "u;u;");
    CHECK(langcore::ExtractFormatSpecs(A<S>("%u%%"), &specs) && specs == "u;");
    CHECK(langcore::ExtractFormatSpecs(A<S>("sem formato"), &specs) && specs.empty());
    CHECK(langcore::ExtractFormatSpecs(A<S>("100%% de %s"), &specs) && specs == "s;");
    CHECK(langcore::ExtractFormatSpecs(A<S>("%5.2f e %-8s e %08d"), &specs) && specs == "f;s;d;");
    CHECK(langcore::ExtractFormatSpecs(A<S>("%I64d %ls %lld"), &specs) && specs == "I64d;ls;lld;");

    // nao suportado
    CHECK(!langcore::ExtractFormatSpecs(A<S>("100%"), &specs));
    CHECK(!langcore::ExtractFormatSpecs(A<S>("%1$s"), &specs));
    CHECK(!langcore::ExtractFormatSpecs(A<S>("%*d"), &specs));
    CHECK(!langcore::ExtractFormatSpecs(A<S>("%.*s"), &specs));
    CHECK(!langcore::ExtractFormatSpecs(A<S>("%n"), &specs));
    CHECK(!langcore::ExtractFormatSpecs(A<S>("% "), &specs));
    CHECK(!langcore::ExtractFormatSpecs(A<S>("%y"), &specs));

    // mesma sequencia
    CHECK(langcore::SameFormatSpecs(A<S>("Save %s?"), A<S>("Guardar %s?")));
    CHECK(langcore::SameFormatSpecs(A<S>("%u%%"), A<S>("%u %%")));
    CHECK(langcore::SameFormatSpecs(A<S>("%d"), A<S>("%5d")));
    CHECK(langcore::SameFormatSpecs(A<S>("read as %s; save as %s"), A<S>("%s ... %s")));
    // a mesma traducao pode repetir o argumento? nao: a contagem tem que bater
    CHECK(!langcore::SameFormatSpecs(A<S>("%s e %s"), A<S>("%s")));
    CHECK(!langcore::SameFormatSpecs(A<S>("%s"), A<S>("%s %s")));
    CHECK(!langcore::SameFormatSpecs(A<S>("%s"), A<S>("")));
    CHECK(!langcore::SameFormatSpecs(A<S>("%d"), A<S>("%u")));
    CHECK(!langcore::SameFormatSpecs(A<S>("%s"), A<S>("%d")));
    CHECK(!langcore::SameFormatSpecs(A<S>("%s"), A<S>("%ls")));
    CHECK(!langcore::SameFormatSpecs(A<S>("%s"), A<S>("%S")));
    CHECK(!langcore::SameFormatSpecs(A<S>("%s %d"), A<S>("%d %s")));
    CHECK(!langcore::SameFormatSpecs(A<S>("%s"), A<S>("50%")));
    CHECK(!langcore::SameFormatSpecs(A<S>("%s"), A<S>("%1$s")));
}

static void TestLanguages()
{
    using namespace langcore;
    std::vector<std::string> c;

    c = BuildLangChain("pt-BR");
    CHECK(c.size() == 3 && c[0] == "pt-BR" && c[1] == "pt" && c[2] == "en");

    c = BuildLangChain("es-AR");
    CHECK(c.size() == 3 && c[0] == "es-AR" && c[1] == "es" && c[2] == "en");

    c = BuildLangChain("zh-Hans-CN");
    CHECK(c.size() == 4 && c[0] == "zh-Hans-CN" && c[1] == "zh-Hans" && c[2] == "zh" && c[3] == "en");

    c = BuildLangChain("en-US");
    CHECK(c.size() == 2 && c[0] == "en-US" && c[1] == "en");

    c = BuildLangChain("en");
    CHECK(c.size() == 1 && c[0] == "en");

    c = BuildLangChain("EN");
    CHECK(c.size() == 1);   // "EN" e "en" sao o mesmo idioma

    c = BuildLangChain("pt_BR");
    CHECK(c.size() == 3 && c[0] == "pt-BR");

    c = BuildLangChain("de");
    CHECK(c.size() == 2 && c[0] == "de" && c[1] == "en");

    // tags invalidos ou vazios caem direto no ingles
    const char* invalid[] = { "", "-", "pt-", "-pt", "pt--BR", "pt BR", "pt/BR", "..\\x", "pt-BR\n",
                              "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa" };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i)
    {
        c = BuildLangChain(invalid[i]);
        CHECK(c.size() == 1 && c[0] == "en");
    }

    CHECK(ResourceNameFor("pt-BR") == "NBLANG_PT_BR");
    CHECK(ResourceNameFor("en") == "NBLANG_EN");
    CHECK(ResourceNameFor("zh-Hans-CN") == "NBLANG_ZH_HANS_CN");
}

template <class S>
static void TestRealFile(const char* path)
{
    std::ifstream in(path, std::ios::binary);
    std::string json((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::unordered_map<S, S> m;

    CHECK(!json.empty());
    CHECK(Parse<S>(json, m));
    CHECK(m.size() > 90);
    CHECK(m.count(A<S>("_info")) == 0 && m.count(A<S>("_locale")) == 0);

    CHECK(m[A<S>("IDM_NEW")] == CP<S>({'&', 'N', 'e', 'w', 9, 'C', 't', 'r', 'l', '+', 'N'}));
    CHECK(m[A<S>("IDM_POPUP_FILE")] == A<S>("&File"));
    CHECK(m[A<S>("IDS_STATUS_COL")] == A<S>("%u%%"));

    // nenhuma chave vazia, nenhum valor vazio, e formatos sempre validos
    for (typename std::unordered_map<S, S>::const_iterator it = m.begin(); it != m.end(); ++it)
    {
        std::string specs;
        CHECK(!it->first.empty() && !it->second.empty());
        CHECK(langcore::ExtractFormatSpecs(it->second, &specs));
    }

    // As strings com formato precisam ser iguais a elas mesmas (sanidade)
    CHECK(langcore::SameFormatSpecs(m[A<S>("IDS_SAVE_PROMPT")], m[A<S>("IDS_SAVE_PROMPT")]));
    CHECK(langcore::SameFormatSpecs(m[A<S>("IDS_ENC_ASK")], A<S>("a %s b %s")));
}

int main(int argc, char** argv)
{
    TestParser<std::u16string>();
    TestParser<std::u32string>();
    TestParser<std::wstring>();

    TestFormatSpecs<std::u16string>();
    TestFormatSpecs<std::u32string>();
    TestFormatSpecs<std::wstring>();

    TestLanguages();

    if (argc > 1)
    {
        TestRealFile<std::u16string>(argv[1]);
        TestRealFile<std::u32string>(argv[1]);
        TestRealFile<std::wstring>(argv[1]);
    }

    std::printf("%d verificacoes, %d falhas\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
