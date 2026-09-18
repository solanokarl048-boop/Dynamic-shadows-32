// IniConfig.h
// Minimal, dependency-free INI reader for the ShadowExtender plugin.
// Written for portability under the Android NDK (no STL streams needed,
// just <cstdio>), and simple enough to audit at a glance.

#pragma once

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

class IniConfig {
public:
    // Loads key=value pairs from an INI file. Sections are flattened into
    // the key as "Section.Key" so lookups stay O(log n) without needing a
    // nested map structure.
    bool Load(const char *path) {
        FILE *f = fopen(path, "r");
        if (!f)
            return false;

        char line[512];
        std::string section;

        while (fgets(line, sizeof(line), f)) {
            std::string raw(line);
            Trim(raw);

            if (raw.empty() || raw[0] == ';' || raw[0] == '#')
                continue; // comment / blank line

            if (raw.front() == '[' && raw.back() == ']') {
                section = raw.substr(1, raw.size() - 2);
                Trim(section);
                continue;
            }

            size_t eq = raw.find('=');
            if (eq == std::string::npos)
                continue;

            std::string key = raw.substr(0, eq);
            std::string val = raw.substr(eq + 1);
            Trim(key);
            Trim(val);

            std::string fullKey = section.empty() ? key : (section + "." + key);
            m_values[fullKey] = val;
        }

        fclose(f);
        return true;
    }

    bool GetBool(const char *section, const char *key, bool def) const {
        std::string v = Get(section, key);
        if (v.empty())
            return def;
        return v == "1" || v == "true" || v == "True" || v == "TRUE" || v == "yes";
    }

    int GetInt(const char *section, const char *key, int def) const {
        std::string v = Get(section, key);
        if (v.empty())
            return def;
        return atoi(v.c_str());
    }

    float GetFloat(const char *section, const char *key, float def) const {
        std::string v = Get(section, key);
        if (v.empty())
            return def;
        return static_cast<float>(atof(v.c_str()));
    }

    std::string GetString(const char *section, const char *key, const char *def) const {
        std::string v = Get(section, key);
        return v.empty() ? std::string(def) : v;
    }

    // Comma-separated list of ints, e.g. "1234,1240,1241" -> vector<int>
    std::vector<int> GetIntList(const char *section, const char *key) const {
        std::vector<int> out;
        std::string v = Get(section, key);
        size_t start = 0;
        while (start < v.size()) {
            size_t comma = v.find(',', start);
            std::string token = (comma == std::string::npos)
                                     ? v.substr(start)
                                     : v.substr(start, comma - start);
            Trim(token);
            if (!token.empty())
                out.push_back(atoi(token.c_str()));
            if (comma == std::string::npos)
                break;
            start = comma + 1;
        }
        return out;
    }

private:
    std::map<std::string, std::string> m_values;

    std::string Get(const char *section, const char *key) const {
        std::string fullKey = std::string(section) + "." + key;
        auto it = m_values.find(fullKey);
        return it == m_values.end() ? std::string() : it->second;
    }

    static void Trim(std::string &s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        if (a == std::string::npos) {
            s.clear();
            return;
        }
        s = s.substr(a, b - a + 1);
    }
};
