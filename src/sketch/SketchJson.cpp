// Sketch <-> JSON for native part files. Field names are part of the file format: add, never rename.

#include "sketch/Sketch.h"

#include <nlohmann/json.hpp>

namespace bl::sketch {
namespace {

const char* curveTypeName(CurveType t) {
    switch (t) {
    case CurveType::Line: return "line";
    case CurveType::Circle: return "circle";
    case CurveType::Arc: return "arc";
    }
    return "line";
}

std::optional<CurveType> curveTypeFrom(const std::string& s) {
    for (CurveType t : {CurveType::Line, CurveType::Circle, CurveType::Arc}) {
        if (s == curveTypeName(t)) {
            return t;
        }
    }
    return std::nullopt;
}

std::optional<ConstraintType> constraintTypeFrom(const std::string& s) {
    for (int i = 0; i <= static_cast<int>(ConstraintType::Diameter); ++i) {
        const auto t = static_cast<ConstraintType>(i);
        if (s == typeName(t)) {
            return t;
        }
    }
    return std::nullopt;
}

} // namespace

nlohmann::json Sketch::toJson() const {
    using nlohmann::json;
    json j;
    j["origin"] = m_origin;
    j["next"] = m_next;
    json counters = json::array();
    for (const auto& [kind, n] : m_counters) {
        counters.push_back({kind, n});
    }
    j["counters"] = counters;
    json points = json::array();
    for (const Point& p : m_points) {
        points.push_back({{"id", p.id},
                          {"x", p.x},
                          {"y", p.y},
                          {"number", p.number},
                          {"owner", p.owner},
                          {"fixed", p.fixed},
                          {"construction", p.construction}});
    }
    j["points"] = points;
    json curves = json::array();
    for (const Curve& c : m_curves) {
        curves.push_back({{"id", c.id},
                          {"type", curveTypeName(c.type)},
                          {"number", c.number},
                          {"p1", c.p1},
                          {"p2", c.p2},
                          {"center", c.center},
                          {"radius", c.radius},
                          {"construction", c.construction}});
    }
    j["curves"] = curves;
    json constraints = json::array();
    for (const Constraint& c : m_constraints) {
        constraints.push_back({{"id", c.id},
                               {"type", typeName(c.type)},
                               {"number", c.number},
                               {"refs", c.refs},
                               {"value", c.value},
                               {"driving", c.driving},
                               {"variant", c.variant},
                               {"anchor", c.anchor},
                               {"labelX", c.labelX},
                               {"labelY", c.labelY}});
    }
    j["constraints"] = constraints;
    return j;
}

bool Sketch::fromJson(const nlohmann::json& j, Sketch& out, std::string* error) {
    try {
        Sketch s;
        s.m_origin = j.at("origin").get<Id>();
        s.m_next = j.at("next").get<Id>();
        s.m_counters.clear();
        for (const auto& kv : j.at("counters")) {
            s.m_counters[kv.at(0).get<int>()] = kv.at(1).get<int>();
        }
        s.m_points.clear();
        for (const auto& p : j.at("points")) {
            Point pt;
            pt.id = p.at("id").get<Id>();
            pt.x = p.at("x").get<double>();
            pt.y = p.at("y").get<double>();
            pt.number = p.value("number", 0);
            pt.owner = p.value("owner", 0);
            pt.fixed = p.value("fixed", false);
            pt.construction = p.value("construction", false);
            s.m_points.push_back(pt);
        }
        for (const auto& c : j.at("curves")) {
            Curve cv;
            cv.id = c.at("id").get<Id>();
            const auto type = curveTypeFrom(c.at("type").get<std::string>());
            if (!type) {
                throw std::runtime_error("unknown curve type " + c.at("type").get<std::string>());
            }
            cv.type = *type;
            cv.number = c.value("number", 0);
            cv.p1 = c.value("p1", 0);
            cv.p2 = c.value("p2", 0);
            cv.center = c.value("center", 0);
            cv.radius = c.value("radius", 0.0);
            cv.construction = c.value("construction", false);
            s.m_curves.push_back(cv);
        }
        for (const auto& c : j.at("constraints")) {
            Constraint k;
            k.id = c.at("id").get<Id>();
            const auto type = constraintTypeFrom(c.at("type").get<std::string>());
            if (!type) {
                throw std::runtime_error("unknown relation " + c.at("type").get<std::string>());
            }
            k.type = *type;
            k.number = c.value("number", 0);
            k.refs = c.at("refs").get<std::vector<Id>>();
            k.value = c.value("value", 0.0);
            k.driving = c.value("driving", true);
            k.variant = c.value("variant", 0);
            k.anchor = c.value("anchor", std::vector<double>{});
            k.labelX = c.value("labelX", 0.0);
            k.labelY = c.value("labelY", 0.0);
            s.m_constraints.push_back(k);
        }
        if (!s.point(s.m_origin)) {
            throw std::runtime_error("the sketch origin is missing");
        }
        out = std::move(s);
        return true;
    } catch (const std::exception& e) {
        if (error) {
            *error = e.what();
        }
        return false;
    }
}

} // namespace bl::sketch
