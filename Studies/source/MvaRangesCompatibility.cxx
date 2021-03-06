/*! Study of correlation matrix, mutual information and Jensen Shannon Divergence to search ranges of mass.
This file is part of https://github.com/hh-italian-group/hh-bbtautau. */

#include <fstream>
#include <functional>
#include <future>
#include <initializer_list>
#include "AnalysisTools/Run/include/program_main.h"
#include "h-tautau/Analysis/include/EventTuple.h"
#include "AnalysisTools/Core/include/exception.h"
#include "AnalysisTools/Core/include/AnalyzerData.h"
#include "AnalysisTools/Core/include/StatEstimators.h"
#include "hh-bbtautau/Analysis/include/MvaVariables.h"
#include "AnalysisTools/Core/include/ProgressReporter.h"
#include "AnalysisTools/Run/include/MultiThread.h"
#include "AnalysisTools/Core/include/NumericPrimitives.h"
#include "hh-bbtautau/Analysis/include/MvaMethods.h"
#include "h-tautau/Cuts/include/Btag_2016.h"
#include "h-tautau/Cuts/include/hh_bbtautau_2016.h"
#include "h-tautau/Analysis/include/AnalysisTypes.h"
#include "hh-bbtautau/Analysis/include/MvaConfigurationReader.h"

struct Arguments { // list of all program arguments
    REQ_ARG(std::string, input_path);
    REQ_ARG(std::string, output_file);
    REQ_ARG(std::string, cfg_file);
    REQ_ARG(std::string, tree_name);
    REQ_ARG(unsigned, number_threads);
    REQ_ARG(size_t, number_variables);
    OPT_ARG(Long64_t, number_events, 1000000);
    OPT_ARG(size_t, number_sets, 0);
    OPT_ARG(size_t, set, 0);
    OPT_ARG(uint_fast32_t, seed, 10000);
};

namespace analysis {
namespace mva_study{

using SampleEntryCollection = std::vector<SampleEntry>;

class MvaClassification {
public:
    using Event = ntuple::Event;
    using EventTuple = ntuple::EventTuple;
    using VecVariables =  std::vector<std::string>;

    SampleIdVarData samples_mass;
    SampleIdNameElement bandwidth, mutual_matrix, correlation_matrix, JSDivergenceSB;

    MvaClassification(const Arguments& _args): args(_args),
        outfile(root_ext::CreateRootFile(args.output_file())), vars(args.number_sets(), args.seed()),
        reporter(std::make_shared<TimeReporter>())
    {
        MvaSetupCollection setups;
        SampleEntryListCollection samples_list;

        ConfigReader configReader;
        MvaConfigReader setupReader(setups);
        configReader.AddEntryReader("SETUP", setupReader, true);
        SampleConfigReader sampleReader(samples_list);
        configReader.AddEntryReader("FILES", sampleReader, false);
        configReader.ReadConfig(args.cfg_file());

        samples = samples_list.at("inputs").files;
    }

    void DistributionJSD_SB(const SampleId& mass_entry, TDirectory* directory) const
    {
        auto mass = ToString(mass_entry);
        auto histo = std::make_shared<TH1D>(("JSD_"+mass+"_Background").c_str(), ("JensenShannonDivergence_Signal"+mass+"_Background").c_str(), 50,0,1);
        histo->SetXTitle("JSD");
        std::ofstream of("InformationTable_"+ToString(mass)+"_"+std::to_string(args.set())+"_"+std::to_string(args.number_sets())+args.tree_name()+".csv", std::ofstream::out);
        of<<"Var_1"<<","<<"Var_2"<<","<<"JSD_ND"<<","<<"JSD_12-(JSD_1+JSD_2)"<<","<<"ScaledMI_Signal_12" <<","<<"ScaledMI_Bkg_12"<<","<<"selected"<<","<<","<<"eliminated by"<<std::endl;
        for(auto& entry : JSDivergenceSB.at(mass_entry)){
             histo->Fill(entry.second);
             root_ext::WriteObject(*histo, directory);
        }
    }

    void PlotJensenShannonSB(TDirectory* directory) const
    {
        std::map<std::string, std::shared_ptr<TGraph>> plot;
        int i = 0;
        for (const auto& entry: JSDivergenceSB){
            if ( entry.first.sampleType == SampleType::Bkg_TTbar )
                continue;
            for (const auto& var : entry.second){
                if (var.first.size()!=1)
                    continue;
                const std::string name = *var.first.begin();
                if (!plot.count(name))
                    plot[name] = CreatePlot("JSD_"+name+"_SignalBkg","JSD_"+name+"_SignalBkg","mass","JSD");
                plot[name]->SetPoint(i, entry.first.mass, var.second);
            }
            i++;
        }
        for(const auto& var: plot){
            root_ext::WriteObject(*var.second, directory);
        }
    }

    void JensenShannonSignalCompatibility(TDirectory* directory) const
    {
        std::map<std::string, std::map<SamplePair, std::future<double>>> JSDss_selected_future;
        for (const auto& var: samples_mass.at(SampleType::Bkg_TTbar)){
            for(auto mass_entry_1 = samples_mass.begin(); mass_entry_1 != samples_mass.end(); ++mass_entry_1) {
                if ( mass_entry_1->first.IsBackground())
                    continue;
                std::vector<const DataVector*> sample_1;
                DataVector band_1;
                sample_1.push_back(&samples_mass.at(mass_entry_1->first).at(var.first));
                band_1.push_back(bandwidth.at(mass_entry_1->first).at(var.first));

                for(auto mass_entry_2 = std::next(mass_entry_1); mass_entry_2 != samples_mass.end(); ++mass_entry_2) {
                    if ( mass_entry_2->first.IsBackground())
                        continue;
                    SamplePair mass_pair(mass_entry_1->first, mass_entry_2->first);
                    std::vector<const DataVector*> sample_2;
                    DataVector band_2;
                    sample_2.push_back(&samples_mass.at(mass_entry_2->first).at(var.first));
                    band_2.push_back(bandwidth.at(mass_entry_2->first).at(var.first));
                    JSDss_selected_future[var.first][mass_pair] = run::async(stat_estimators::JensenShannonDivergence_ND<double>, sample_1, sample_2, band_1, band_2);
                }
            }
        }
        for(auto& futures : JSDss_selected_future) {
            auto JSDss_selected = CollectFutures(futures.second);
            CreateMatrixHistosCompatibility(samples_mass, JSDss_selected, futures.first+"_Signal_compatibility_JSD", directory);
        }
    }

    void KolmogorovSignalCompatibility(TDirectory* directory) const
    {
        for (const auto& var: samples_mass.at(SampleType::Bkg_TTbar)){
            std::map<SamplePair, double> kolmogorov;
            for(auto mass_entry_1 = samples_mass.begin(); mass_entry_1 != samples_mass.end(); ++mass_entry_1) {
                if ( mass_entry_1->first.IsBackground())
                    continue;
                for(auto mass_entry_2 = std::next(mass_entry_1); mass_entry_2 != samples_mass.end(); ++mass_entry_2) {
                    if ( mass_entry_2->first.IsBackground())
                        continue;
                    SamplePair mass_pair(mass_entry_1->first, mass_entry_2->first);
                    std::vector<double> vector_signal = samples_mass.at(mass_entry_1->first).at(var.first);
                    std::sort(vector_signal.begin(), vector_signal.end());\
                    std::vector<double> vector_signal_2 = samples_mass.at(mass_entry_2->first).at(var.first);
                    std::sort(vector_signal_2.begin(), vector_signal_2.end());
                    Double_t* v_s = vector_signal.data(), *v_s_2 = vector_signal_2.data();
                    kolmogorov[mass_pair]  = TMath::KolmogorovTest(static_cast<int>(vector_signal.size()), v_s,
                                                                   static_cast<int>(vector_signal_2.size()), v_s_2, "");
                }
            }
            CreateMatrixHistosCompatibility(samples_mass, kolmogorov, var.first+"_Signal_compatibility_KS", directory);
        }
    }

    SetNamesVar FindBestVariables(SampleId mass, std::map<std::string, std::string>& eliminated) const
    {
        static constexpr double threashold_mi = 0.7;
        const NameElement& JSDivergenceND = JSDivergenceSB.at(mass);
        const NameElement& mutual_matrix_signal = mutual_matrix.at(mass);
        const NameElement& mutual_matrix_bkg = mutual_matrix.at(SampleType::Bkg_TTbar);
        SetNamesVar selected, not_corrected;
        VectorName_ND JSDivergence_vector(JSDivergenceND.begin(), JSDivergenceND.end());

        std::ofstream best_entries_file("Best_entries_"+ToString(mass)+"_"+std::to_string(args.set())+"_"+std::to_string(args.number_sets())+args.tree_name()+".csv", std::ofstream::out);
        best_entries_file<<","<<","<<"JSD_sb"<<","<<"MI_sgn"<<","<<"Mi_bkg"<<std::endl;

        static const std::map<std::pair<size_t, size_t>, std::string> prefixes = {
            { { 0, 0 }, "" }, { { 0, 1 }, "*" }, { { 1, 0 }, "-" }
        };
        while(selected.size() < args.number_variables() && JSDivergence_vector.size()) {
            std::sort(JSDivergence_vector.begin(), JSDivergence_vector.end(), [](const auto& el1, const auto& el2){
                return el1.second > el2.second;
            });
            auto best_entry = JSDivergence_vector.front();
            for(const auto& name : best_entry.first) {
                const auto counts = std::make_pair(selected.count(name), not_corrected.count(name));
                const auto& prefix = prefixes.at(counts);
                best_entries_file << prefix << name << prefix << ",";
                if(counts.first || counts.second)
                    continue;
                const double JS_1d = JSDivergenceND.at(name);
                for(auto& entry : JSDivergence_vector) {
                    if(entry.first.count(name))
                        entry.second -= JS_1d;
                }
                for(const auto& other_entry : samples_mass.at(mass)) {
                    if(other_entry.first == name) continue;
                    const Name_ND names({name, other_entry.first});
                    if(mutual_matrix_signal.at(names) < threashold_mi && mutual_matrix_bkg.at(names) < threashold_mi){
                        eliminated[other_entry.first] = "MI-"+name;
                        not_corrected.insert(other_entry.first);
                    }
                }
                selected.insert(name);
            }
            if (best_entry.first.IsSubset(selected)){
                    best_entries_file << JSDivergenceND.at(best_entry.first) << "," << mutual_matrix_signal.at(best_entry.first)
                           << "," << mutual_matrix_bkg.at(best_entry.first) ;
            }
            best_entries_file<<std::endl;
            JSDivergence_vector = CopySelectedVariables(JSDivergence_vector, best_entry.first, not_corrected);
        }
        best_entries_file.close();
        return selected;
    }


    void InformationTable(const SampleId& mass_entry, const SetNamesVar& selected, const std::map<std::string, std::string>& eliminated) const
    {
        auto mass = ToString(mass_entry);
        std::ofstream of("InformationTable_"+ToString(mass)+"_"+std::to_string(args.set())+"_"+std::to_string(args.number_sets())+args.tree_name()+".csv", std::ofstream::out);
        of<<"Var_1"<<","<<"Var_2"<<","<<"JSD_ND"<<","<<"JSD_12-(JSD_1+JSD_2)"<<","<<"ScaledMI_Signal_12" <<","<<"ScaledMI_Bkg_12"<<","<<"selected"<<","<<","<<"eliminated by"<<std::endl;
        for(auto& entry : JSDivergenceSB.at(mass_entry)){
             if (entry.first.size() == 1){
                 auto name_1 = entry.first.at(0);
                 bool selected_1 = selected.count(name_1);
                 of<<name_1<<","<<"   "<<","<<JSDivergenceSB.at(mass_entry).at(name_1)<<","<<","<<","<<","<<selected_1;
                 if(eliminated.count(name_1)) of<<","<<","<<eliminated.at(name_1);
                 of<<std::endl;
             }
             else  if (entry.first.size() == 2){
                 auto name_1 = entry.first.at(0);
                 auto name_2 = entry.first.at(1);
                 bool selected_1 = selected.count(name_1);
                 bool selected_2 = selected.count(name_2);
                 of<<name_1<<","<<name_2<<","<<JSDivergenceSB.at(mass_entry).at(entry.first)<<","
                  <<JSDivergenceSB.at(mass_entry).at(entry.first)-(JSDivergenceSB.at(mass_entry).at(name_1)+JSDivergenceSB.at(mass_entry).at(name_2))
                  <<","<<mutual_matrix.at(mass_entry).at(entry.first)<<","<<mutual_matrix.at(SampleType::Bkg_TTbar).at(entry.first)<<","<<selected_1<<","<<selected_2;
                 if(eliminated.count(name_1)) of<<","<<eliminated.at(name_1);
                 if(eliminated.count(name_2)) of<<","<<eliminated.at(name_2);
                 of<<std::endl;
             }
        }
        of.close();
    }

    void CreateMatrixIntersection(const SampleIdSetNamesVar& mass_selected) const
    {
        std::cout << std::endl << "Intersection" << std::endl;
        int bin = static_cast<int>(mass_selected.size());
        auto matrix_intersection = std::make_shared<TH2D>("Intersection", "Intersection of variables", bin, 0, bin, bin, 0, bin);
        matrix_intersection->SetXTitle("mass");
        matrix_intersection->SetYTitle("mass");
        int k = 1;
        for(const auto& mass_1: mass_selected){
            int j = 1;
            std::string mass = ToString(mass_1.first);
            matrix_intersection->GetXaxis()->SetBinLabel(k, (mass).c_str());
            matrix_intersection->GetYaxis()->SetBinLabel(k, (mass).c_str());
            for(const auto& mass_2: mass_selected){
                VecVariables intersection;
                std::set_intersection(mass_1.second.begin(), mass_1.second.end(), mass_2.second.begin(), mass_2.second.end(),
                                        std::back_inserter(intersection));
                matrix_intersection->SetBinContent(k, j, intersection.size());
                j++;
            }
            k++;
        }
        root_ext::WriteObject(*matrix_intersection, outfile.get());;
    }

    void VariablesSelection() const
    {
        SampleIdSetNamesVar mass_selected;
        std::ofstream ofs("SelectedVariable_"+std::to_string(args.set())+"_"+std::to_string(args.number_sets())+args.tree_name()+".csv", std::ofstream::out);
        for (const auto& mass_entry : samples_mass){
            if (mass_entry.first.IsBackground())
                continue;
            ofs << ToString(mass_entry.first) << std::endl;
            std::cout << ToString(mass_entry.first) << " " << std::flush;
            std::map<std::string, std::string> eliminated;
            mass_selected[mass_entry.first] =  FindBestVariables(mass_entry.first, eliminated);
            for(auto& entry_selected: mass_selected[mass_entry.first]){
                    ofs << entry_selected<<",";
            }
            ofs << std::endl;
            InformationTable(mass_entry.first, mass_selected.at(mass_entry.first), eliminated);
        }
        CreateMatrixIntersection(mass_selected);
    }

    void LoadData()
    {
        for(const SampleEntry& entry : samples)
        {
            if ( entry.channel != "" && args.tree_name() != entry.channel ) continue;
            auto input_file = root_ext::OpenRootFile(args.input_path()+"/"+entry.filename);
            EventTuple tuple(args.tree_name(), input_file.get(), true, {} , GetMvaBranches());
            Long64_t tot_entries = 0;
            for(Long64_t current_entry = 0; tot_entries < args.number_events() && current_entry < tuple.GetEntries(); ++current_entry) {
                tuple.GetEntry(current_entry);
                const Event& event = tuple.data();
                if (static_cast<EventEnergyScale>(event.eventEnergyScale) != EventEnergyScale::Central || (event.q_1+event.q_2) != 0 || event.jets_p4.size() < 2
                    || event.extraelec_veto == true || event.extramuon_veto == true || event.jets_p4[0].eta() > cuts::btag_2016::eta
                    || event.jets_p4[1].eta() > cuts::btag_2016::eta)
                    continue;
                auto bb = event.jets_p4[0] + event.jets_p4[1];
                if (!cuts::hh_bbtautau_2016::hh_tag::IsInsideEllipse(event.SVfit_p4.mass(), bb.mass()))
                    continue;
                tot_entries++;
                vars.AddEvent(event, entry.id, entry.weight);
            }
            std::cout << entry << " number of events: " << tot_entries << std::endl;
        }
        TimeReport();
        samples_mass = vars.GetSampleVariables(args.set());
    }

    void Run()
    {
        run::ThreadPull threads(args.number_threads());
        LoadData();

        std::cout << "n.variabili: " << samples_mass.at(SampleType::Bkg_TTbar).size() << std::endl;
        std::cout << "n.masse segnale: " << samples_mass.size() - 1 << " + " << 1 << " fondo."<<std::endl;
        std::cout << "Bandwidth and Mutual Information" << std::endl;
        auto directory_mutinf = root_ext::GetDirectory(*outfile, "MutualInformation");
        auto directory_jensenshannon =root_ext::GetDirectory(*outfile, "JensenShannonDivergence");
        auto directory_jenshan_sgnlbkg = root_ext::GetDirectory(*directory_jensenshannon, "Signal_Background");
        auto directory_jenshan_distrib = root_ext::GetDirectory(*directory_jenshan_sgnlbkg,"Distribution");

        for (const auto& sample: samples_mass){
            std::cout<<"----"<<ToString(sample.first)<<"----"<<" entries: "<<sample.second.at("pt_l1").size()<<std::endl;
            std::cout<<"correlation  " << std::flush;
            correlation_matrix[sample.first] = Correlation(sample.second);
            std::cout<<"bandwidth  "<<std::flush;
            bandwidth[sample.first] = OptimalBandwidth(sample.second);
            std::cout<<"mutual  "<< std::flush;
            mutual_matrix[sample.first] = Mutual(sample.second, bandwidth.at(sample.first));
            if (sample.first.sampleType == SampleType::Bkg_TTbar){
                TimeReport();
                continue;
            }
            std::cout<<"mutual plot  "<< std::flush;
            MutualHisto(sample.first, mutual_matrix.at(sample.first), mutual_matrix.at(SampleType::Bkg_TTbar), directory_mutinf);
            std::cout<<"JSD_sgn_bkg  "<< std::flush;
            JSDivergenceSB[sample.first] = JensenDivergenceSB(sample.second, samples_mass.at(SampleType::Bkg_TTbar), bandwidth.at(sample.first), bandwidth.at(SampleType::Bkg_TTbar));
            DistributionJSD_SB(sample.first, directory_jenshan_distrib);
            TimeReport();
        }
        std::cout <<"Mutual histos" << std::endl;
        auto directory_mutinf_matrix = root_ext::GetDirectory(*directory_mutinf, "Matrix");
        CreateMatrixHistos(samples_mass, mutual_matrix, "MI", directory_mutinf_matrix);
        std::cout <<"Correlation histos" << std::endl;
        auto directory_correlation = root_ext::GetDirectory(*outfile, "Correlation");
        CreateMatrixHistos(samples_mass, correlation_matrix, "correlation", directory_correlation, true);

        auto directory_mi_corr = root_ext::GetDirectory(*outfile, "MI_Correlation");
        for(const auto& sample : samples_mass){
            std::string mass ="MI_Correlation_"+ToString(sample.first);
            auto histo = std::make_shared<TH2D>(mass.c_str(), mass.c_str(),50,0,1,50,0,1);
            histo->SetXTitle("1-MI");
            histo->SetYTitle("Correlation");
            for (const auto& pair: mutual_matrix.at(sample.first)){
                histo->Fill(pair.second, std::abs(correlation_matrix.at(sample.first).at(pair.first)));
            }
            root_ext::WriteObject(*histo, directory_mi_corr);
        }
        auto directory_jenshan_matrix = root_ext::GetDirectory(*directory_jenshan_sgnlbkg, "Matrix");
        CreateMatrixHistos(samples_mass, JSDivergenceSB, "JSD", directory_jenshan_matrix);
        std::cout<<"Plot Jensen Shannon"<<std::endl;
        auto directory_jenshan_allvars = root_ext::GetDirectory(*directory_jenshan_sgnlbkg, "All_variables");
        PlotJensenShannonSB(directory_jenshan_allvars);

        std::cout<<"Signal Compatibility Jensen Shannon  "<<std::flush;
        auto directory_jenshan_sgnlsgnl = root_ext::GetDirectory(*directory_jensenshannon, "Signal_Signal");
        JensenShannonSignalCompatibility(directory_jenshan_sgnlsgnl);
        TimeReport();
        std::cout<<"Signal Compatibility Kolmogorov"<<std::flush;
        auto directory_ks = root_ext::GetDirectory(*outfile, "Kolmogorov");
        KolmogorovSignalCompatibility(directory_ks);
        TimeReport();

        std::cout<<"Selection variables"<<std::endl;
        VariablesSelection();
        TimeReport(true);
    }

    void TimeReport(bool tot = false) const
    {
        reporter->TimeReport(tot);
    }

private:
    Arguments args;
    SampleEntryCollection samples;
    std::shared_ptr<TFile> outfile;
    MvaVariablesStudy vars;
    std::shared_ptr<TimeReporter> reporter;
};
}
}

PROGRAM_MAIN(analysis::mva_study::MvaClassification, Arguments) // definition of the main program function
