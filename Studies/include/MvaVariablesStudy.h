/*! Definition of MvaVariablesStudy class, the main class for Mva studies.
This file is part of https://github.com/hh-italian-group/hh-bbtautau. */

#pragma once

#include "hh-bbtautau/Analysis/include/MvaVariables.h"

namespace  analysis {
namespace mva_study{


using DataVector = std::vector<double>;
using VarData = std::map<std::string, DataVector>;
using SampleIdVarData = std::map<SampleId, VarData>;


class MvaVariablesStudy : public MvaVariables {
private:
    std::map<std::string, double> variables;
    using SplittedSampleIdVarData = std::map<size_t, SampleIdVarData>;
    SplittedSampleIdVarData all_variables;

public:
    using MvaVariables::MvaVariables;
    virtual void SetValue(const std::string& name, double value, char /*type = 'F'*/) override
    {
        variables[name] = value;
    }

    virtual void AddEventVariables(size_t set, const SampleId& mass,  double /*weight*/) override
    {
        VarData& sample_vars = all_variables[set][mass];
        for(const auto& name_value : variables) {
            const std::string& name = name_value.first;
            const double value = name_value.second;
            sample_vars[name].push_back(value);
        }
    }

    const SampleIdVarData& GetSampleVariables(size_t set = 0) const
    {
        if(set >= all_variables.size())
            throw exception("Sample part is out of range.");
        return all_variables.at(set);
    }

    virtual std::shared_ptr<TMVA::Reader> GetReader() override {throw exception ("GetReader not supported.");}
};

inline const std::set<std::string>& GetMvaBranches()
{
    static const std::set<std::string> EnabledBranches_read = {
        "eventEnergyScale", "q_1", "q_2", "jets_p4", "extraelec_veto", "extramuon_veto ", "SVfit_p4",
        "pfMET_p4", "p4_1", "p4_2", "channelId"
    };
    return EnabledBranches_read;
}

struct Name_ND{
private:
    std::set<std::string> names;

public:
    using const_iterator = std::set<std::string>::const_iterator;

    Name_ND(const std::string& name) { names.insert(name); }
    Name_ND(std::initializer_list<std::string> _names) : names(_names.begin(), _names.end()){}

    bool operator<(const Name_ND& x) const
    {
        if (names.size() != x.names.size()) return names.size()<x.names.size();
        if (names.size() == 0) return false;
        auto x_iter = x.names.begin();
        for(auto iter = names.begin(); iter != names.end(); ++iter, ++x_iter){
            if(*iter != *x_iter) return *iter < *x_iter;
        }
        return false;
    }

    const_iterator begin() const { return names.begin(); }
    const_iterator end() const { return names.end(); }
    size_t size() const { return names.size(); }
    size_t count(const std::string& name) const { return names.count(name); }
    void insert(const std::string& name) { names.insert(name); }

    const std::string& at(size_t n) const
    {
        if(n >= size())
            throw exception("Number of name dimensions = %1%, which is less than %2%") % size() % n;
        return *std::next(begin(), static_cast<std::iterator_traits<const_iterator>::difference_type>(n));
    }

    template<typename Set>
    bool IsSubset(const Set& set) const
    {
        for(const auto& name : names)
            if(!set.count(name)) return false;
        return true;
    }

    bool operator==(const Name_ND& other) const
    {
        if (names.size() != other.size()) return false;
        auto iter = names.begin(), other_iter = other.names.begin();
        for(; iter != names.end(); ++iter, ++other_iter) {
            if(*iter != *other_iter) return false;
        }
        return true;
    }

    bool operator !=(const Name_ND& other) const { return !(*this == other); }

    std::string ToString(const std::string& sep = "_") const
    {
        std::ostringstream ss;
        auto iter = names.begin();
        if(iter != names.end())
            ss << *iter;
        for(iter = std::next(iter); iter != names.end(); ++iter)
            ss << sep << *iter;
        return ss.str();
    }
};
}
}
