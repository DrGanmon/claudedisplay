#include "../src/org_id.h"
#include <assert.h>
#include <string>

int main() {
  const std::string uuid = "01234567-89ab-cdef-0123-456789abcdef";
  assert(findOrganizationUuid(uuid.c_str(), uuid.size()) == 0);
  const std::string label = "Mein Konto [" + uuid + "]";
  assert(findOrganizationUuid(label.c_str(), label.size()) == 12);
  const std::string prefix = "https://claude.ai/api/organizations/";
  const std::string url = prefix + uuid + "/usage";
  assert(findOrganizationUuid(url.c_str(), url.size()) == prefix.size());
  const std::string upper = "01234567-89AB-CDEF-0123-456789ABCDEF";
  assert(findOrganizationUuid(upper.c_str(), upper.size()) == 0);
  for (const std::string &value : {std::string(), std::string("Meine Organisation"),
       uuid.substr(1), std::string("g1234567-89ab-cdef-0123-456789abcdef"),
       std::string("01234567889ab-cdef-0123-456789abcdef")}) {
    assert(findOrganizationUuid(value.c_str(), value.size()) == value.size());
  }
  const std::string two = "[" + uuid + "] [" + upper + "]";
  assert(findOrganizationUuid(two.c_str(), two.size()) == 1);
}
