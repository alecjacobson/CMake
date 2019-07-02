/* Distributed under the OSI-approved BSD 3-Clause License.  See accompanying
   file Copyright.txt or https://cmake.org/licensing for details.  */
#include "cmExportLibraryDependenciesCommand.h"

#include "cmsys/FStream.hxx"
#include <map>
#include <utility>

#include "cm_memory.hxx"

#include "cmGeneratedFileStream.h"
#include "cmGlobalGenerator.h"
#include "cmMakefile.h"
#include "cmStateTypes.h"
#include "cmSystemTools.h"
#include "cmTarget.h"
#include "cmTargetLinkLibraryType.h"
#include "cmake.h"

class cmExecutionStatus;

bool cmExportLibraryDependenciesCommand::InitialPass(
  std::vector<std::string> const& args, cmExecutionStatus&)
{
  if (args.empty()) {
    this->SetError("called with incorrect number of arguments");
    return false;
  }

  // store the arguments for the final pass
  this->Filename = args[0];
  this->Append = false;
  if (args.size() > 1) {
    if (args[1] == "APPEND") {
      this->Append = true;
    }
  }
  return true;
}

void cmExportLibraryDependenciesCommand::FinalPass()
{
  // export_library_dependencies() shouldn't modify anything
  // ensure this by calling a const method
  this->ConstFinalPass();
}

void cmExportLibraryDependenciesCommand::ConstFinalPass() const
{
  // Use copy-if-different if not appending.
  std::unique_ptr<cmsys::ofstream> foutPtr;
  if (this->Append) {
    const auto openmodeApp = std::ios::app;
    foutPtr =
      cm::make_unique<cmsys::ofstream>(this->Filename.c_str(), openmodeApp);
  } else {
    std::unique_ptr<cmGeneratedFileStream> ap(
      new cmGeneratedFileStream(this->Filename, true));
    ap->SetCopyIfDifferent(true);
    foutPtr = std::move(ap);
  }
  std::ostream& fout = *foutPtr;

  if (!fout) {
    cmSystemTools::Error("Error Writing " + this->Filename);
    cmSystemTools::ReportLastSystemError("");
    return;
  }

  // Collect dependency information about all library targets built in
  // the project.
  cmake* cm = this->Makefile->GetCMakeInstance();
  cmGlobalGenerator* global = cm->GetGlobalGenerator();
  const std::vector<cmMakefile*>& locals = global->GetMakefiles();
  std::map<std::string, std::string> libDepsOld;
  std::map<std::string, std::string> libDepsNew;
  std::map<std::string, std::string> libTypes;
  for (cmMakefile* local : locals) {
    for (auto const& tgt : local->GetTargets()) {
      // Get the current target.
      cmTarget const& target = tgt.second;

      // Skip non-library targets.
      if (target.GetType() < cmStateEnums::STATIC_LIBRARY ||
          target.GetType() > cmStateEnums::MODULE_LIBRARY) {
        continue;
      }

      // Construct the dependency variable name.
      std::string targetEntry = target.GetName();
      targetEntry += "_LIB_DEPENDS";

      // Construct the dependency variable value with the direct link
      // dependencies.
      std::string valueOld;
      std::string valueNew;
      cmTarget::LinkLibraryVectorType const& libs =
        target.GetOriginalLinkLibraries();
      for (cmTarget::LibraryID const& li : libs) {
        std::string ltVar = li.first;
        ltVar += "_LINK_TYPE";
        std::string ltValue;
        switch (li.second) {
          case GENERAL_LibraryType:
            valueNew += "general;";
            ltValue = "general";
            break;
          case DEBUG_LibraryType:
            valueNew += "debug;";
            ltValue = "debug";
            break;
          case OPTIMIZED_LibraryType:
            valueNew += "optimized;";
            ltValue = "optimized";
            break;
        }
        std::string lib = li.first;
        if (cmTarget* libtgt = global->FindTarget(lib)) {
          // Handle simple output name changes.  This command is
          // deprecated so we do not support full target name
          // translation (which requires per-configuration info).
          if (const char* outname = libtgt->GetProperty("OUTPUT_NAME")) {
            lib = outname;
          }
        }
        valueOld += lib;
        valueOld += ";";
        valueNew += lib;
        valueNew += ";";

        std::string& ltEntry = libTypes[ltVar];
        if (ltEntry.empty()) {
          ltEntry = ltValue;
        } else if (ltEntry != ltValue) {
          ltEntry = "general";
        }
      }
      libDepsNew[targetEntry] = valueNew;
      libDepsOld[targetEntry] = valueOld;
    }
  }

  // Generate dependency information for both old and new style CMake
  // versions.
  const char* vertest =
    "\"${CMAKE_MAJOR_VERSION}.${CMAKE_MINOR_VERSION}\" GREATER 2.4";
  fout << "# Generated by CMake\n\n";
  fout << "if(" << vertest << ")\n";
  fout << "  # Information for CMake 2.6 and above.\n";
  for (auto const& i : libDepsNew) {
    if (!i.second.empty()) {
      fout << "  set(\"" << i.first << "\" \"" << i.second << "\")\n";
    }
  }
  fout << "else()\n";
  fout << "  # Information for CMake 2.4 and lower.\n";
  for (auto const& i : libDepsOld) {
    if (!i.second.empty()) {
      fout << "  set(\"" << i.first << "\" \"" << i.second << "\")\n";
    }
  }
  for (auto const& i : libTypes) {
    if (i.second != "general") {
      fout << "  set(\"" << i.first << "\" \"" << i.second << "\")\n";
    }
  }
  fout << "endif()\n";
}
