#pragma once

#include "echo.hpp"
#include <string>

#include <set>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

struct Emerald {
    struct Storage;
    struct Compile;
    struct EchoExtension;
};

struct EmeraldUnit;

template<typename T>
struct EmeraldInit {
    using value = T;
    using pointer = T*;
    inline static std::map<std::string, std::function<pointer()>> Inits{};
    EmeraldInit(std::string name, std::function<pointer()> init) {
        Inits.emplace(name, init);
    }
};

struct TagChecker {
    private:
    struct context {
        std::vector<std::string>* row;
        int index;

        const std::string& operator*() const {
            return row->at(index);
        }

        context& operator++() {
            index++;
            return *this;
        }

        context operator++(int) {
            index++;
            return context{row, index - 1};
        }
    };

    bool check_val(context& cnt) {
        if (*cnt == "&")
            return check_and(cnt);
        if (*cnt == "|")
            return check_or(cnt);
        if (*cnt == "!")
            return check_not(cnt);
        return checker(*(cnt++));
    }

    bool check_and(context& cnt) {
        return check_val(++cnt) && check_val(++cnt);
    }

    bool check_or(context& cnt) {
        return check_val(++cnt) || check_val(++cnt);
    }

    bool check_not(context& cnt) {
        return !check_val(++cnt);
    }

    public:
    using TagCheckerFunction = std::function<bool(std::string)>;

    TagChecker(TagCheckerFunction checker) : checker(checker) {}

    TagCheckerFunction checker;

    std::vector<std::string> parse(std::string script) {
        std::vector<std::string> req;
        req.emplace_back("");
        for (auto i : script) {
            if (i == ' ') {
                if (req[req.size() - 1].size() != 0)
                    req.emplace_back("");
            } else {
                req[req.size() - 1] += i;
            }
        }
        return req;
    }

    bool check(std::string script) {
        auto req = parse(script);
        return check(req);
    }

    bool check(std::vector<std::string>& script) {
        auto cnt = context{&script, 0};
        return check_val(cnt);
    }
};

struct EmeraldStatic {
private:
    inline static std::vector<std::function<void()>> Coll_{};
public:
    EmeraldStatic(std::function<void()> a) {
       Coll_.emplace_back(a);
    }

    static void Init() {
        std::cout << "Begin static init!" << std::endl;
        for (auto i : Coll_) {
            std::cout << '.' << std::endl;
            i();
        }
    }
};

enum class CompilationRound {
    SetupOutput,
    SwapSource,
};

class CompilationService {
public:
    virtual void CompilationMsg(CompilationRound round, const EmeraldUnit& reffered) = 0;
    virtual void init_mappings(echolang::echo_mapping* map) = 0;
};

class InnerSelector : public CompilationService {
public:
    virtual bool Satisfies(fs::directory_entry file) = 0;
};

class Sorter : public CompilationService {
public:
    virtual bool PathLessCompare(const fs::path& a, const fs::path& b) = 0;
    bool operator()(const fs::path& a, const fs::path& b) {
        return this->PathLessCompare(a, b);
    }
};

class OuterSelector : public CompilationService {
public:
    virtual bool Satisfies(fs::path file, int index, const EmeraldUnit& src) = 0;
};

class Namer : public CompilationService {
public:
    virtual std::string MakeName(fs::path file, int index, const EmeraldUnit& src) = 0;
};

struct EmeraldUnit {
private:
    static bool IsAttribute(const std::string& name, const std::string& tag) {
        if (tag.size() < name.size() + 1)
            return false;
        
        if (tag[0] != AttributeSymbol)
            return false;

        for (int i = 0; i < name.size(); i++) {
            if (tag[i + 1] != name[i])
                return false;
        }

        if (tag.size() == name.size() + 1)
            return true;
            
        return tag[name.size() + 1] == AttributeValueSpacerSymbol;
    }

public:
    constexpr const static char AttributeSymbol = '@';
    constexpr const static char AttributeValueSpacerSymbol = ':';
    inline static const std::string UnitInitFile = "Unit.emerald";

    std::string Name;
    std::string Path;
    std::set<std::string> Tags;

    InnerSelector* FInSelector{nullptr};
    Sorter* FSorter{nullptr};
    OuterSelector* FOutSelector{nullptr};
    Namer* FNamer{nullptr};

    EmeraldUnit() {}
    EmeraldUnit(fs::path path) : Path(path) {
        auto init_path = path/UnitInitFile;
        if (fs::exists(init_path)) {
            FromScript(init_path);
        }
    }

    static bool IsUnit(fs::path path) {
        return fs::exists(path/UnitInitFile);
    }

void init_mappings(echolang::echo_mapping* map) {
    map->mappings["Name"] = echolang::echo_field{Name};
    {
        auto tag = echolang::echo_set_field{Tags};
        tag.mappings["ContainsAttribute"] = echolang::echo_single_shot{echo_lambda(&) { 
            return ContainsAttribute(row.value);
        }};
        map->mappings["Tags"] = tag;
    }
    {
        auto obj = echolang::echo_object<InnerSelector>{this->FInSelector};
        for (auto& i : EmeraldInit<InnerSelector>::Inits)
            obj.inits[i.first]=i.second;
        map->mappings["InnerSelector"] = obj;
    }
    {
        auto obj = echolang::echo_object<Sorter>{this->FSorter};
        for (auto& i : EmeraldInit<Sorter>::Inits)
            obj.inits[i.first]=i.second;
        map->mappings["Sorter"] = obj;
    }
    {
        auto obj = echolang::echo_object<OuterSelector>{this->FOutSelector};
        for (auto& i : EmeraldInit<OuterSelector>::Inits)
            obj.inits[i.first]=i.second;
        map->mappings["OuterSelector"] = obj;
    }
    {
        auto obj = echolang::echo_object<Namer>{this->FNamer};
        for (auto& i : EmeraldInit<Namer>::Inits)
            obj.inits[i.first]=i.second;
        map->mappings["Namer"] = obj;
    }
}

    static EmeraldUnit* CreateEmpty() {
        return new EmeraldUnit();
    }

    void FromScript(fs::path file) {
        auto sc = std::make_shared<echolang::echo_script>();
        sc->from_file(file);

        auto map = echolang::echo_mapping::create_default_controls();
        init_mappings(&map);

        echolang::executor exec{sc, map};
        exec.run();
    }

    bool ContainsAttribute(const std::string& name) const {
        auto it = Tags.lower_bound(AttributeSymbol + name + AttributeValueSpacerSymbol);

        if (it == Tags.end())
            return false;

        return IsAttribute(name, *it);
    }

    const std::string& GetAttribute(const std::string& name) const {
        return *Tags.lower_bound(AttributeSymbol + name + AttributeValueSpacerSymbol);
    }

    static bool IsValuedAttribute(const std::string& attr) {
        return attr.find(AttributeValueSpacerSymbol) != std::string::npos;
    }

    static std::string ValueFromAttribute(const std::string& attr) {
        return attr.substr(attr.find(AttributeValueSpacerSymbol));
    }

    void SetAttribute(const std::string& name, std::string&& value) {
        if (ContainsAttribute(name))
            Tags.erase(GetAttribute(name));

        Tags.emplace(AttributeSymbol + name + AttributeValueSpacerSymbol + value);
    }

    ~EmeraldUnit() {
    }
};

struct Emerald::Storage {
    inline static std::vector<EmeraldUnit> Units{};

    inline static fs::path StashPath;

    static void SetStashPath(std::string path) {
        StashPath = path;
    }

    static void LoadUnit(std::string path) {
        std::cout << "Loading unit:" << path << std::endl;
        if (EmeraldUnit::IsUnit(StashPath/path))
            Units.push_back(EmeraldUnit{StashPath/path});
    }

    static void ListTags() {
        std::map<std::string, size_t> tags;
        
        for (auto unit : Units) {
            for (auto tag : unit.Tags) {
                if (tag[0] != EmeraldUnit::AttributeSymbol)
                    tags[tag]++;
            }
        }

        for (auto tag : tags) {
            std::cout << std::setw(10) << tag.first << ": " << tag.second << std::endl;
        }
    }

    static void LoadUnitsFrom(std::string path) {
        std::cout << "Loading units from: " << StashPath/path << std::endl;
        for (auto entry : fs::directory_iterator{StashPath/path}) {
            if (entry.is_directory()) {
                LoadUnit(fs::relative(entry.path(), StashPath).string());
            }
        }
    }

    static void SetupStorageMappings(echolang::echo_mapping* mapping) {
        mapping->mappings["LoadUnit"] = echolang::echo_bind_function(LoadUnit);
        mapping->mappings["LoadUnitsFrom"] = echolang::echo_bind_function(LoadUnitsFrom);
        mapping->mappings["SetStashPath"] = echolang::echo_bind_function(SetStashPath);
        mapping->mappings["ListTags"] = echolang::echo_bind_function(ListTags);
    }
};

struct Emerald::Compile {
    inline static fs::path Output{"Output"};

    inline static std::vector<EmeraldUnit*> Targets;

    static void SetOutputPath(std::string path) {
        Output = path;
        if (!fs::exists(Emerald::Storage::StashPath/Output))
            fs::create_directory(Emerald::Storage::StashPath/Output);
    }

    static void GenerateFreeOutputFolder() {
        int indexer = 0;
        while (fs::exists(Emerald::Storage::StashPath/Output)) {
            Output = "Output" + std::to_string(indexer);
        }
        fs::create_directory(Emerald::Storage::StashPath/Output);
    }

    static bool Select() {
        std::cout << "Input polish notation request:\n";
        std::string a;
        auto osize = Targets.size();
        std::getline(std::cin, a);
        if (a == "stop")
            return false;
        EmeraldUnit* trg;
        auto check = [&trg](std::string tag) { return trg->Tags.contains(tag); };
        TagChecker checker{check};
        auto scr = checker.parse(a);
        for (auto& i : Emerald::Storage::Units) {
            trg = &i;
            if (checker.check(scr))
                Targets.push_back(&i);
        }
        std::cout << "Selected " << Targets.size() - osize << " targets.\n";
        return true;
    }

    static void RemoveSame() {
        std::sort(Targets.begin(), Targets.end());
        int j = 0;
        for (int i = 1; i < Targets.size(); i++) {
            if (Targets[i] != Targets[j]) {
                if (i != j + 1)
                    Targets[j + 1] = Targets[i];
                j++;
            }
        }
        int osize = Targets.size();
        Targets.resize(j + 1);
        std::cout << "Duplicated targets removed(" << osize << " -> " << Targets.size() << ")\n";
    }

    static void ClearTargets() {
        Targets.clear();
        std::cout << "Targets cleared\n";
    }

    static void ClearOutput() {
        size_t totalFiles = std::distance(fs::directory_iterator{Emerald::Storage::StashPath/Output}, fs::directory_iterator{});
        std::cout << "Clearing output(" << totalFiles << ")\n";

        size_t files = 0;
        size_t p = 0;
        size_t iterations = (totalFiles + 99) / 100;
        for(auto i : fs::directory_iterator{Emerald::Storage::StashPath/Output}) {
            fs::remove(i.path());
            files++;
            if(files * iterations/totalFiles > p) {
                p = files * iterations/totalFiles;
                std::cout << "\t" << p * 100 / iterations << "% Done\033[100D";
            }
        }
    }

static void CompileOutput() {
    auto output_path = Emerald::Storage::StashPath/Output;
    
    EmeraldStatic::Init();

    size_t totalUnits = Targets.size();
    size_t units = 0;

    std::cout << "Compiling " << Targets.size() << " targets" << std::endl;

    for (auto unit : Targets) {
        std::cout << "Compiling: " << unit->Name;
        std::cout << "\t" << (++units) * 100 / totalUnits << "% Done\033[100D";

        unit->FInSelector->CompilationMsg(CompilationRound::SwapSource, *unit);
        unit->FSorter->CompilationMsg(CompilationRound::SwapSource, *unit);
        unit->FOutSelector->CompilationMsg(CompilationRound::SwapSource, *unit);
        unit->FNamer->CompilationMsg(CompilationRound::SwapSource, *unit);

        std::vector<fs::path> files;

        for (auto file : fs::directory_iterator(Emerald::Storage::StashPath/unit->Path)) {
            if (unit->FInSelector->Satisfies(file))
                files.push_back(file.path());
        }
        
        auto comp = [&unit](const fs::path& a, const fs::path& b) {
            return unit->FSorter->PathLessCompare(a, b);
        };
        std::sort(files.begin(), files.end(), comp);

        for (int index = 0; index < files.size(); index++) {
            if (unit->FOutSelector->Satisfies(files[index], index, *unit)) {
                auto name = unit->FNamer->MakeName(files[index], index, *unit);

                auto destenation = output_path/name;

                fs::copy(files[index], destenation);
            }
        }
    }
}

    static void SetupCompileMappings(echolang::echo_mapping* mapping) {
        mapping->mappings["CompileOutput"] = echolang::echo_bind_function(CompileOutput);
        mapping->mappings["ClearOutput"] = echolang::echo_bind_function(ClearOutput);
        mapping->mappings["ClearTargets"] = echolang::echo_bind_function(ClearTargets);
        mapping->mappings["RemoveSame"] = echolang::echo_bind_function(RemoveSame);
        mapping->mappings["Select"] = echolang::echo_bind_function(Select);
        mapping->mappings["SetOutputPath"] = echolang::echo_bind_function(SetOutputPath);
        mapping->mappings["GenerateFreeOutputFolder"] = echolang::echo_bind_function(GenerateFreeOutputFolder);
    }
};

struct Emerald::EchoExtension {
    static void SetupEmeraldMappings(echolang::echo_mapping* mapping) {
        {
            echolang::echo_mapping compile;
            Emerald::Compile::SetupCompileMappings(&compile);
            mapping->mappings["Compile"] = compile;
        }
        {
            echolang::echo_mapping storage;
            Emerald::Storage::SetupStorageMappings(&storage);
            mapping->mappings["Storage"] = storage;
        }
    }
};

class RegexSelector : public InnerSelector {
private:
    std::regex Expr_;
public:
    std::string Expression;

    void CompilationMsg(CompilationRound round, const EmeraldUnit& reffered) {
        std::cout.flush();
        Expr_ = std::regex{Expression};
    }

    void init_mappings(echolang::echo_mapping* map) {
        map->mappings["Expr"] = echolang::echo_field{Expression};
        map->mappings["Check"] = echolang::echo_single_shot{echo_lambda(&) { return std::regex_match(row.value, std::regex{Expression}); }};
    }

    bool Satisfies(fs::directory_entry file) {
        if (!file.is_regular_file())
            return false;
        return std::regex_match(file.path().filename().c_str(), Expr_);
    }

    static InnerSelector* CreateDefault() {
        return new RegexSelector{};
    }
};

class DefaultSelector : public InnerSelector {
public:

    void CompilationMsg(CompilationRound round, const EmeraldUnit& reffered) {}

    void init_mappings(echolang::echo_mapping* map) {}

    bool Satisfies(fs::directory_entry file) {
        if (!file.is_regular_file())
            return false;
        return file.path().extension() != ".emerald";
    }

    static InnerSelector* CreateDefault() {
        return new DefaultSelector{};
    }
};

class FilenameSorter : public Sorter {
public:
    void CompilationMsg(CompilationRound round, const EmeraldUnit& reffered) {}

    void init_mappings(echolang::echo_mapping* map) {}

    bool PathLessCompare(const fs::path& a, const fs::path& b) {
        return a.filename() < b.filename();
    }

    static Sorter* CreateDefault() {
        return new FilenameSorter{};
    }
};

class OuterSelectAll : public OuterSelector {
public:
    void CompilationMsg(CompilationRound round, const EmeraldUnit& reffered) {}

    void init_mappings(echolang::echo_mapping* map) {
    }

    bool Satisfies(fs::path file, int index, const EmeraldUnit& src) {
        return true;
    }

    static OuterSelector* CreateDefault() {
        return new OuterSelectAll{};
    }
};

// TODO: Finish Taged outer selector 

class OuterSelectTaged : public OuterSelector {
public:
    void CompilationMsg(CompilationRound round, const EmeraldUnit& reffered) {}

    void init_mappings(echolang::echo_mapping* map) {}

    bool Satisfies(fs::path file, int index, const EmeraldUnit& src) {
        return true;
    }

    static void TagRequest() {

    }

    static OuterSelector* CreateDefault() {
        return new OuterSelectTaged{};
    }
};

class ThroughNamer : public Namer {
private:
    inline static int Index_{0};

public:
    bool AddName{false};

    void CompilationMsg(CompilationRound round, const EmeraldUnit& reffered) {}

    void init_mappings(echolang::echo_mapping* map) {
        map->mappings["AddName"] = echolang::echo_flag_field{AddName};
    }

    std::string MakeName(fs::path file, int index, const EmeraldUnit& src) {
        return std::to_string(++Index_) + " [" + src.Name + "]" + file.extension().c_str();
    }

    static void UpdateIndexer() {
        Index_ = 0;
    }
    
    static Namer* CreateDefault() {
        return new ThroughNamer();
    }
};

namespace {
    auto s0 = EmeraldInit<InnerSelector>("regex_selector", RegexSelector::CreateDefault);
    auto s1 = EmeraldInit<InnerSelector>("default", DefaultSelector::CreateDefault);
    auto s2 = EmeraldInit<Sorter>{"default", FilenameSorter::CreateDefault};
    auto s3 = EmeraldInit<OuterSelector>{"default", OuterSelectAll::CreateDefault};
    auto s4 = EmeraldInit<Namer>{"through", ThroughNamer::CreateDefault};

    auto k1 = EmeraldStatic{ThroughNamer::UpdateIndexer};
    //auto k2 = EmeraldStatic{OuterSelectTaged::TagRequest};
}

