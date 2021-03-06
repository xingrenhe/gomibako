#include <string>
#include <unordered_map>
#include <map>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <memory>
#include <ctime>
#include <cstdio>
#include <yaml-cpp/yaml.h>
#include <crow.h>
#include "util/yaml.h"
#include "gomibako.h"
#include "article.h"

using namespace gomibako;

ArticleManager::ArticleManager(const std::string &articles_path) {
    this->metadata_path = articles_path + "/metadata.yaml";
    this->content_path = articles_path + "/content/";
}

bool ArticleManager::load_metadata() {
    using namespace std;
    YAML::Node root;
    try {
        root = YAML::LoadFile(this->metadata_path);
    } catch (const YAML::BadFile &e) {
        return false;
    }
    if (!root.IsSequence() && !root.IsNull()) {
        return false;
    }
    this->id_metadata_map.clear();
    this->timestamp_id_pairs.clear();
    if (root.IsSequence()) {
        for (YAML::const_iterator it = root.begin(); it != root.end(); ++it) {
            const ArticleMetadata &metadata = it->as<ArticleMetadata>();
            this->id_metadata_map[metadata.id] = metadata;
            this->timestamp_id_pairs.emplace_back(metadata.timestamp, metadata.id);
        }
    }
    update_tags();
    sort_metadata();
    if (this->on_update) {
        this->on_update(this);
    }
    return true;
}

bool ArticleManager::get_metadata(const std::string &id, ArticleMetadata &metadata) const {
    auto &&it = this->id_metadata_map.find(id);
    if (it == this->id_metadata_map.end()) {
        return false;
    } else {
        metadata = it->second;
        return true;
    }
}

bool ArticleManager::get_metadata(const std::vector<std::string> &ids,
                                  std::vector<ArticleMetadata> &metadata) const {
    metadata.clear();
    metadata.reserve(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        auto &&it = this->id_metadata_map.find(ids[i]);
        if (it == this->id_metadata_map.end()) {
            return false;
        }
        metadata.push_back(it->second);
    }
    return true;
}

bool ArticleManager::get_content(const std::string &id, std::ostringstream &out) const {
    using namespace std;
    auto &&it = this->id_metadata_map.find(id);
    if (it == this->id_metadata_map.end()) {
        return false;
    }
    ifstream fs(this->content_path + it->second.filename);
    if (!fs) {
        return false;
    }
    out << fs.rdbuf();
    fs.close();
    return true;
}

bool ArticleManager::get_content(const std::vector<std::string> &ids,
                                 std::vector<std::ostringstream> &out) const {
    using namespace std;
    out.resize(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        auto &&it = this->id_metadata_map.find(ids[i]);
        if (it == this->id_metadata_map.end()) {
            return false;
        }
        ifstream fs(this->content_path + it->second.filename);
        if (!fs) {
            return false;
        }
        out[i] << fs.rdbuf();
        fs.close();
    }
    return true;
}

bool ArticleManager::save_metadata() {
    YAML::Emitter out;
    out << YAML::BeginSeq;
    for (auto &&i : this->timestamp_id_pairs) {
        YAML::Node metadata(this->id_metadata_map.at(i.second));
        out << metadata;
    }
    out << YAML::EndSeq;
    if (!out.good()) {
        return false;
    }
    std::ofstream fs(this->metadata_path);
    if (!fs) {
        return false;
    }
    fs << out.c_str();
    fs.close();
    if (this->on_update) {
        this->on_update(this);
    }
    return true;
}

void ArticleManager::sort_metadata() {
    std::sort(this->timestamp_id_pairs.begin(), this->timestamp_id_pairs.end(), compare_timestamp);
}

std::string ArticleManager::add_article(const std::string &title, const std::string &content,
                                        time_t timestamp, const std::set<std::string> &tags) {
    const std::string &id = generate_id(title);
    const std::string &filename = generate_filename(id);
    ArticleMetadata metadata;
    metadata.id = id;
    metadata.title = title;
    metadata.tags = tags;
    metadata.timestamp = timestamp;
    metadata.filename = filename;
    std::ofstream fs(this->content_path + filename);
    if (!fs) {
        return "";
    }
    fs << content;
    fs.close();
    this->id_metadata_map[id] = std::move(metadata);
    this->timestamp_id_pairs.emplace_back(metadata.timestamp, id);
    sort_metadata();
    save_metadata();
    update_tags();
    return id;
}

bool ArticleManager::delete_article(const std::string &id, bool delete_file) {
    auto &&it = this->id_metadata_map.find(id);
    if (it == this->id_metadata_map.end()) {
        return false;
    }
    auto &&itpair = std::equal_range(this->timestamp_id_pairs.begin(),
                                     this->timestamp_id_pairs.end(),
                                     std::make_pair(it->second.timestamp, it->second.id),
                                     compare_timestamp);
    for (auto i = itpair.first; i != itpair.second; ++i) {
        if (i->second == id) {
            this->timestamp_id_pairs.erase(i);
            break;
        }
    }
    save_metadata();
    update_tags();
    if (delete_file) {
        const std::string &filename = this->content_path + it->second.filename;
        remove(filename.c_str());
    }
    return true;
}

bool ArticleManager::edit_article(const std::string &id, const std::string &title, time_t timestamp, 
                                  const std::set<std::string> &tags, const std::string &content) {
    auto &&it = this->id_metadata_map.find(id);
    if (it == this->id_metadata_map.end()) {
        return false;
    }
    auto &&itpair = std::equal_range(this->timestamp_id_pairs.begin(),
                                     this->timestamp_id_pairs.end(),
                                     std::make_pair(it->second.timestamp, it->second.id),
                                     compare_timestamp);
    for (auto i = itpair.first; i != itpair.second; ++i) {
        if (i->second == id) {
            this->timestamp_id_pairs.erase(i);
            break;
        }
    }
    this->timestamp_id_pairs.emplace_back(timestamp, id);
    std::ofstream fs(this->content_path + it->second.filename);
    if (!fs) {
        return false;
    }
    fs << content;
    fs.close();
    it->second.title = title;
    it->second.timestamp = timestamp;
    it->second.tags = tags;
    sort_metadata();
    update_tags();
    return save_metadata();
}

void ArticleManager::apply_filter(const Filter &filter, std::vector<std::string> &ids) const {
    std::shared_ptr<const TimeIDMap> out = filter(this->timestamp_id_pairs, this->id_metadata_map);
    if (!out) {
        return;
    }
    ids.clear();
    ids.reserve(out->size());
    for (auto &&i : *out) {
        ids.push_back(i.second);
    }
}

void ArticleManager::apply_filters(const std::vector<Filter> &filters,
                                   std::vector<std::string> &ids) const {
    if (filters.size() == 0) {
        return;
    } else if (filters.size() == 1) {
        apply_filter(filters[0], ids);
        return;
    }
    std::shared_ptr<const TimeIDMap> out = filters[0](this->timestamp_id_pairs, this->id_metadata_map);
    for (size_t i = 1; i < filters.size(); ++i) {
        if (!out) {
            return;
        }
        out = filters[i](*out, this->id_metadata_map);
    }
    ids.clear();
    if (!out) {
        return;
    }
    ids.reserve(out->size());
    for (auto &&i : *out) {
        ids.push_back(i.second);
    }
}

std::string ArticleManager::generate_id(const std::string &title) {
    if (this->id_metadata_map.count(title) == 0) {
        return title;
    }
    std::ostringstream ss;
    for (int i = 1; ; ++i) {
        ss.str("");
        ss << title << "-" << i;
        if (this->id_metadata_map.count(ss.str()) == 0) {
            break;
        }
    }
    return ss.str();
}

std::string ArticleManager::generate_filename(const std::string &id) {
    return crow::utility::base64encode_urlsafe(id.c_str(), id.length()) + ".txt";
}

void ArticleManager::update_tags() {
    this->tags.clear();
    for (auto &&i : this->id_metadata_map) {
        for (auto &&tag : i.second.tags) {
            if (this->tags.count(tag)) {
                ++this->tags[tag];
            } else {
                this->tags[tag] = 1;
            }
        }
    }
}

PageManager::PageManager(const std::string &pages_path_) : pages_path(pages_path_) {}

bool PageManager::load_pages() {
    using namespace std;
    YAML::Node root;
    try {
        root = YAML::LoadFile(this->pages_path);
    } catch (const YAML::BadFile &e) {
        return false;
    }
    if (!root.IsSequence() && !root.IsNull()) {
        return false;
    }
    this->pages.clear();
    if (root.IsSequence()) {
        for (YAML::const_iterator it = root.begin(); it != root.end(); ++it) {
            CustomPage page;
            if (!extract_yaml_map(*it,
                make_pair("order", &page.order),
                make_pair("id", &page.id),
                make_pair("title", &page.title),
                make_pair("content", &page.content)
                )) {
                return false;
            }
            this->pages.push_back(page);
        }
    }
    sort_pages();
    return true;
}

void PageManager::sort_pages() {
    std::sort(this->pages.begin(), this->pages.end(), [] (const CustomPage &a, const CustomPage &b) {
        return a.order < b.order;
    });
}

std::string PageManager::add_page(int order, const std::string &title,
                                  const std::string &content) {
    const std::string &id = generate_id(title);
    this->pages.push_back({order, title, id, content});
    sort_pages();
    save_pages();
    return id;
}

bool PageManager::get_page(const std::string &id, CustomPage &page) const {
    auto &&it = this->pages.begin();
    while (it != this->pages.end()) {
        if (it->id == id) {
            page = *it;
            return true;
        }
        ++it;
    }
    return false;
}

bool PageManager::edit_page(const std::string &id, int order, const std::string &title,
                            const std::string &content) {
    auto &&it = std::find_if(this->pages.begin(), this->pages.end(), [&id](const CustomPage &p) {
        return p.id == id;
    });
    if (it == this->pages.end()) {
        return false;
    }
    it->title = title;
    it->content = content;
    it->order = order;
    sort_pages();
    return save_pages();
}

bool PageManager::save_pages() {
    YAML::Emitter out;
    out << YAML::BeginSeq;
    for (auto &&i : this->pages) {
        out << YAML::BeginMap
            << YAML::Key << "id" << YAML::Value << i.id
            << YAML::Key << "order" << YAML::Value << i.order
            << YAML::Key << "title" << YAML::Value << i.title
            << YAML::Key << "content" << YAML::Value << i.content
            << YAML::EndMap;
    }
    out << YAML::EndSeq;
    if (!out.good()) {
        return false;
    }
    std::ofstream fs(this->pages_path);
    if (!fs) {
        return false;
    }
    fs << out.c_str();
    fs.close();
    return true;
}

bool PageManager::delete_page(const std::string &id) {
    auto &&it = std::find_if(this->pages.begin(), this->pages.end(), [&id](const CustomPage &p) {
        return p.id == id;
    });
    if (it == this->pages.end()) {
        return false;
    }
    this->pages.erase(it);
    sort_pages();
    return save_pages();
}

std::string PageManager::generate_id(const std::string &title) {
    if (std::count_if(this->pages.begin(), this->pages.end(), [&title] (const CustomPage &page) {
        return page.id == title;
    }) == 0) {
        return title;
    }
    std::ostringstream ss;
    for (int i = 1; ; ++i) {
        ss.str("");
        ss << title << "-" << i;
        if (std::count_if(this->pages.begin(), this->pages.end(), [&ss] (const CustomPage &page) {
            return page.id == ss.str();
        }) == 0) {
            break;
        }
    }
    return ss.str();
}
