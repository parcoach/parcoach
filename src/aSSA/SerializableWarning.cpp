#include "parcoach/SerializableWarning.h"

#include "llvm/IR/Function.h"
#include "llvm/Support/JSON.h"
#include "llvm/Support/SourceMgr.h"

using namespace llvm;

namespace parcoach::serialization::sonar {

namespace {
Twine createWarningMessage(Function const &F) {
  return F.getName() + " may not be called by all MPI processes";
}
} // namespace

Warning::Warning(parcoach::Warning const &W)
    : Message{W.Where.Filename.str(),
              {W.Where.Line},
              createWarningMessage(W.MissedFunction).str()} {
  Conditionals.reserve(W.Conditionals.size());
  for (auto const &Cond : W.Conditionals) {
    Conditionals.emplace_back(
        Location{Cond.Filename.str(),
                 {Cond.Line},
                 "because this condition depends on the rank"});
  }
}

void Database::append(WarningCollection const &Warnings) {
  Issues.reserve(Issues.size() + Warnings.size());
  for (auto const &[_, W] : Warnings) {
    Issues.emplace_back(W);
  }
}

bool fromJSON(json::Value const &E, TextRange &R, json::Path P) {
  json::ObjectMapper O(E, P);
  return O && O.map("startLine", R.Line);
}

bool fromJSON(json::Value const &E, Location &L, json::Path P) {
  json::ObjectMapper O(E, P);
  return O && O.map("message", L.Message) && O.map("filePath", L.Filename) &&
         O.map("textRange", L.Range);
}

json::Value toJSON(TextRange const &R) {
  return json::Object{
      {"startLine", R.Line},
  };
}

json::Value toJSON(Location const &L) {
  return json::Object{
      {"filePath", L.Filename},
      {"textRange", L.Range},
      {"message", L.Message},
  };
}

bool fromJSON(json::Value const &E, Warning &W, json::Path P) {
  json::ObjectMapper O(E, P);
  return O && O.map("primaryLocation", W.Message) &&
         O.map("secondaryLocations", W.Conditionals);
}

json::Value toJSON(Warning const &W) {
  return json::Object{
      // matches SonarQube format at the moment.
      {"engineId", "parcoach"},       {"ruleId", "mpiCollective"},
      {"severity", "MINOR"},          {"type", "BUG"},
      {"primaryLocation", W.Message}, {"secondaryLocations", W.Conditionals},
  };
}

bool fromJSON(json::Value const &E, Database &DB, json::Path P) {
  json::ObjectMapper O(E, P);
  return O && O.map("issues", DB.Issues);
}

json::Value toJSON(Database const &DB) {
  return json::Object{
      {"issues", DB.Issues},
  };
}

Expected<Database> Database::load(StringRef Json) {
  return json::parse<Database>(Json);
}

void Database::write(raw_fd_ostream &Os) const {
  Os << formatv("{0:2}", json::Value(*this));
}

} // namespace parcoach::serialization::sonar
