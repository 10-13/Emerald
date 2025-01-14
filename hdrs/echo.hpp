#include <iostream>
#include <regex>
#include <vector>
#include <map>
#include <set>
#include <string>
#include <fstream>
#include <sstream>
#include <memory>
#include <functional>

#define echo_lambda(catch) [catch](echolang::echo_row row)

namespace {
    template<typename R, typename... Args>
    using Function = R(Args...);
}


namespace echolang {

struct echo_row {
    std::string name;
    std::string value;
    int space;

    echo_row(std::string name) : name(name), value(""), space(0) {}
    echo_row(std::string name, std::string value) : name(name), value(value), space(0) {}
    echo_row(std::string name, std::string value, int space) : name(name), value(value), space(space) {}
};

class echo_script {
private:
    void from_iterateble_structure(auto begin, auto end) {
        std::regex tag_match("(?:\\n|^)( *)\\[([^$\\]]*)(?:\\$([^\\]]*))?\\]");

        auto tags_begin = std::sregex_iterator(begin, end, tag_match);
        auto tags_end = std::sregex_iterator();

        for (auto i = tags_begin; i != tags_end; i++) {
            std::smatch match = *i;
            data.emplace_back(match[2],match[3],match[1].length());
        }
    }

public:
    std::vector<echo_row> data;

    echo_script() {}

    void from_row(std::string row) {
        from_iterateble_structure(row.begin(), row.end());
    }

    void from_file(std::string path) {
        std::ifstream file(path);

        std::stringstream buffer;
        buffer << file.rdbuf();

        std::string a = buffer.str();

        from_iterateble_structure(a.begin(), a.end());
    }

    auto extract_group(int begin) {
        if (data[begin].space <= data[begin + 1].space)
            return echo_script{};
        begin++;

        echo_script script;
        int end = begin;
        while(data[end].space >= data[begin].space)
            script.data.emplace_back(data[end++]);
        return script;
    }

    friend std::ostream& operator<<(std::ostream& out, echo_script& s) {
        std::cout << "Script:\n";
        for (auto i : s.data) {
            std::cout << '[' << i.space << ']' << i.name << '{' << i.value << "}\n";
        }
        return out;
    }

    friend class executor;
};

using echo_func = std::function<bool(echo_row)>;

class executor {
private:
    bool pos_exsists(int pos) {
        return pos < script->data.size();
    }

    int next_same_level_pos(int pos) {
        int l = script->data[pos].space;
        for (int i = pos + 1; i < script->data.size() && script->data[i].space >= l; i++)
            if (script->data[i].space == l)
                return i;
        return -1;
    }

public:
    echo_func echo;
    std::stack<int> position;
    std::shared_ptr<echo_script> script;

    executor(std::shared_ptr<echo_script> trg, echo_func echo) : script(trg), echo(echo) {}

    void run(int pos = 0) {
        position.emplace(pos);
        while(!position.empty() && position.top() < script->data.size()) {
            //std::cout << "EXEC:" << position.top() << std::endl;
            auto& i = script->data[position.top()];
            bool r = echo(i);
            if (!r) {
                position.top() = next_same_level_pos(position.top());
                if (position.top() == -1) {
                    position.pop();
                }
            } else if (pos_exsists(position.top() + 1)) {
                if (script->data[position.top() + 1].space > i.space) {
                    position.emplace(position.top() + 1);
                }
            }
        }
    }

    int exit_position(int start) {
        int r = start;
        while (script->data[r].space >= script->data[start].space)
            r++;
        return r;
    }
};

struct echo_path {
    constexpr const static char spacer = '/';

    static std::string first(std::string str) {
        size_t i = str.find(spacer);
        if (i == -1)
            return str;
        return str.substr(0, i);
    }

    static echo_row remove_first(echo_row r) {
        size_t i = r.name.find(spacer);
        if (i == std::string::npos)
            return echo_row{"", r.value, r.space};
        r.name = r.name.substr(i + 1);
        return r;
    }

    static std::string remove_first(std::string str) {
        size_t i = str.find(spacer);
        if (i == std::string::npos)
            return "";
        return str.substr(i + 1);
    }
};

struct echo_mapping {
    std::map<std::string, echo_func> mappings;

    bool operator()(echo_row row) {
        std::string path = echo_path::first(row.name);

        if (mappings.find(path) == mappings.end())
            return false;

        return mappings[path](echo_path::remove_first(row));
    }

    static echo_mapping create_default_controls();
};

struct echo_single_shot {
    echo_func function;
    bool shot{true};

    bool operator()(echo_row row) {
        if (row.value == "update") {
            shot = true;
            return false;
        }

        if(shot) {
            shot = false;
            return function(row);
        }
        return false;
    }
};

struct echo_multi_shot {
    echo_func function;
    size_t shot{1};

    bool operator()(echo_row row) {
        if(shot != 0) {
            shot--;
            return function(row);
        }
        return false;
    }
};

struct echo_field : public echo_mapping {
    echo_field(std::string& str) {
        this->mappings["set"] = [&](echo_row row) {str = row.value; return false;};
        this->mappings["cout"] = [&](echo_row row) { std::cout << str; return false;};
        this->mappings["coutn"] = [&](echo_row row) { std::cout << str << std::endl; return false;};
        this->mappings["clear"] = [&](echo_row row) {str = std::string{}; return false;};
        this->mappings["is_empty"] = echo_single_shot{[&](echo_row row) -> bool { return str.empty();}};
    }
};

struct echo_flag_field : public echo_mapping {
    echo_flag_field(bool& flag) {
        this->mappings["set"] = [&](echo_row row) { 
            flag = true; 
            return false;
        };
        this->mappings["reset"] = [&](echo_row row) {
            flag = false;
            return false;
        };
        this->mappings["check"] = echo_single_shot{[&](echo_row row)->bool{ return flag;}};
        this->mappings["cout"] = [&](echo_row row) { std::cout << std::boolalpha << flag; return false;};
        this->mappings["coutn"] = [&](echo_row row) { std::cout << std::boolalpha << flag << std::endl; return false;};
    }
};

struct echo_set_field : public echo_mapping {
    echo_set_field(std::set<std::string>& set) {
        this->mappings["add"] = [&](echo_row row) { 
            set.emplace(row.value);
            return false;
        };
        this->mappings["remove"] = [&](echo_row row) {
            set.erase(row.value);
            return false;
        };
        this->mappings["contains"] = echo_single_shot{
            [&](echo_row row)->bool{ 
                return set.find(row.value) != set.end();
            }
        };
        this->mappings["coutn"] = [&](echo_row row) {
            for (auto i : set)
                std::cout << row.value << i << std::endl;
            return false;
        };
        this->mappings["coutsize"] = [&](echo_row row) {
            std::cout << set.size();
            return false;
        };
    }
};

template <typename T>
concept echo_object_type = requires (T* x, echo_mapping* mapping) { 
    x->init_mappings(mapping);
};

template<echo_object_type T>
struct echo_object : public echo_mapping {
    using value_ptr = T*;
    value_ptr& ptr;
    std::map<std::string, std::function<T*()>> inits;

    echo_object(value_ptr& ref) : ptr(ref) {}

    void add_field(std::string name, std::string& field) {
        this->mappings[name] = echo_field{field};
    }

    bool operator()(echo_row row) {
        if (ptr == nullptr) {
            if (inits.find(row.value) == inits.end())
                return false;

            ptr = inits[row.value]();
            ptr->init_mappings(this);
            return true;
        }

        return echo_mapping::operator()(row);
    }
};

struct echo_cycle {
    size_t times = 0ULL - 2ULL;

    bool operator()(echo_row row) {
        if (row.value == "update") {
            times = 0ULL - 2ULL;
            return false;
        }

        if (times == 0ULL - 2) {
            times = std::stoull(row.value);
        }

        times--;

        if (times == 0ULL - 1)
            return false;
        return true;
    }
};

echo_mapping echo_mapping::create_default_controls() {
    echo_mapping res;
    echo_mapping echo_map;
    {
        echo_map.mappings["cycle"] = echo_cycle{};
        echo_map.mappings["single"] = echo_single_shot{};
        echo_map.mappings["cout"] = [](echo_row row){std::cout << row.value; return false;};
        echo_map.mappings["coutn"] = [](echo_row row){std::cout << row.value << std::endl; return false;};
        echo_map.mappings["log"] = [](echo_row row){std::cout << "[" << row.space << "]" << row.value << std::endl;return false;};
        echo_map.mappings["answer"] = [](echo_row row){std::cout << row.value << "(1 - to accept, other - deceline):\n"; int a; std::cin >> a; return a == 1; };
    }
    res.mappings["echo"] = echo_map;
    return res;
}

echo_func echo_bind_function(::Function<void> func) {
    return [=](echo_row row)->bool{func(); return false;};
}
echo_func echo_bind_function(::Function<bool> func) {
    return [=](echo_row row)->bool{return func();};
}
echo_func echo_bind_function(::Function<void, std::string> func) {
    return [=](echo_row row)->bool{func(row.value); return false;};
}
echo_func echo_bind_function(::Function<bool, std::string> func) {
    return [=](echo_row row)->bool{return func(row.value);};
}

namespace generic {

template<typename T>
using echo_generic_func = std::function<bool(T& target, echo_row req)>;

template<typename T>
echo_func create_echo_generic_map(std::map<std::string, T>& map, echo_generic_func<T> echo) {
    return [&map, echo](echo_row row)->bool {
        std::string path = echo_path::first(row.name);

        if (path.size() == 0) {
            auto p = echo_path::first(row.value);
            auto outer = echo_path::remove_first(row.value);

            if (p == "clear") {
                map.clear();
            }
            if (p == "add") {
                map[outer] = T{};
            }
            if (p == "remove") {
                map.erase(outer);
            }
            if (p == "contains") {
                return map.find(outer) != map.end();
            }
            if (p == "coutn") {
                for (auto i : map) {
                    std::cout << outer << i.first << ": ";
                    echo(map[i.first], echo_row{"cout"});
                    std::cout << std::endl;
                }
            }
            if (p == "coutsize") {
               std::cout << map.size();
            }
            
            return false;
        }

        if (map.find(path) == map.end())
            return false;

        return echo(map[path], echo_path::remove_first(row));
    };
}



template<typename T>
struct echo_generic_mapping {
    std::map<std::string, echo_generic_func<T>> mappings;

    using value_type = T;
    using reference_type = T&;

    bool operator()(T& target, echo_row row) {
        std::string path = echo_path::first(row.name);

        if (mappings.find(path) == mappings.end())
            return false;

        return mappings[path](target, echo_path::remove_first(row));
    }
};


struct echo_generic_field : public echo_generic_mapping<std::string> {
    echo_generic_field() : echo_generic_mapping<value_type>() {
        this->mappings["set"] = [](value_type& str, echo_row row) {str = row.value; return false;};
        this->mappings["cout"] = [](value_type& str, echo_row row) { std::cout << str; return false;};
        this->mappings["coutn"] = [](std::string& str, echo_row row) { std::cout << str << std::endl; return false;};
        this->mappings["clear"] = [](std::string& str, echo_row row) {str = std::string{}; return false;};
    }
};

struct echo_generic_range_field : public echo_generic_mapping<std::pair<int, int>> {
    echo_generic_range_field() : echo_generic_mapping<value_type>() {
        this->mappings["set_begin"] = [](value_type& str, echo_row row) { str.first = stoi(row.value); return false;};
        this->mappings["set_end"] = [](value_type& str, echo_row row) { str.second = stoi(row.value); return false;};
        this->mappings["cout"] = [](value_type& str, echo_row row) { std::cout << str.first << " -> " << str.second; return false;};
        this->mappings["coutn"] = [](value_type& str, echo_row row) { std::cout  << str.first << " -> " << str.second << std::endl; return false;};
    }
};

}

}