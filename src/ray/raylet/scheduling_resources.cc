#include "scheduling_resources.h"

#include <cmath>
#include <sstream>

#include "ray/util/logging.h"

namespace ray {

namespace raylet {

bool EqualsZeroEpsilon(double quantity) {
  if ((quantity <= EPSILON) && (quantity >= -1 * EPSILON)) {
    return true;
  }
  return false;
}

bool EqualsOneEpsilon(double quantity) {
  if ((quantity <= 1 + EPSILON) && (quantity >= 1 - EPSILON)) {
    return true;
  }
  return false;
}

ResourceSet::ResourceSet() {}

ResourceSet::ResourceSet(const std::unordered_map<std::string, double> &resource_map)
    : resource_capacity_(resource_map) {
  for (auto const &resource_pair : resource_capacity_) {
    RAY_CHECK(resource_pair.second > 0 + EPSILON)
        << "Resource " << resource_pair.first << " capacity is " << resource_pair.second
        << ". Should have been greater than zero.";
  }
}

ResourceSet::ResourceSet(const std::vector<std::string> &resource_labels,
                         const std::vector<double> resource_capacity) {
  RAY_CHECK(resource_labels.size() == resource_capacity.size());
  for (uint i = 0; i < resource_labels.size(); i++) {
    RAY_CHECK(resource_capacity[i] > 0 + EPSILON)
        << "Resource " << resource_labels[i] << " capacity is " << resource_capacity[i]
        << ". Should have been greater than zero.";
    resource_capacity_[resource_labels[i]] = resource_capacity[i];
  }
}

ResourceSet::~ResourceSet() {}

bool ResourceSet::operator==(const ResourceSet &rhs) const {
  return (this->IsSubset(rhs) && rhs.IsSubset(*this));
}

bool ResourceSet::IsEmpty() const {
  // Check whether the capacity of each resource type is zero. Exit early if not.
  return resource_capacity_.empty();
}

bool ResourceSet::IsSubset(const ResourceSet &other) const {
  // Check to make sure all keys of this are in other.
  for (const auto &resource_pair : resource_capacity_) {
    const auto &resource_name = resource_pair.first;
    const double lhs_quantity = resource_pair.second;
    double rhs_quantity = other.GetResource(resource_name);
    if (lhs_quantity > rhs_quantity) {
      // Resource found in rhs, but lhs capacity exceeds rhs capacity.
      return false;
    }
  }
  return true;
}

/// Test whether this ResourceSet is a superset of the other ResourceSet
bool ResourceSet::IsSuperset(const ResourceSet &other) const {
  return other.IsSubset(*this);
}

/// Test whether this ResourceSet is precisely equal to the other ResourceSet.
bool ResourceSet::IsEqual(const ResourceSet &rhs) const {
  return (this->IsSubset(rhs) && rhs.IsSubset(*this));
}

bool ResourceSet::RemoveResource(const std::string &resource_name) {
  throw std::runtime_error("Method not implemented");
}
void ResourceSet::SubtractResources(const ResourceSet &other) {
  // Subtract the resources and delete any if new capacity is zero.
  for (const auto &resource_pair : other.GetResourceMap()) {
    const std::string &resource_label = resource_pair.first;
    const double &resource_capacity = resource_pair.second;
    if (resource_capacity_.count(resource_label) == 1) {
      resource_capacity_[resource_label] -= resource_capacity;
      if (resource_capacity_[resource_label] < 0 + EPSILON) {
        resource_capacity_.erase(resource_label);
      }
    }
  }
}

void ResourceSet::SubtractResourcesStrict(const ResourceSet &other) {
  // Subtract the resources, make sure none goes below zero and delete any if new capacity
  // is zero.
  for (const auto &resource_pair : other.GetResourceMap()) {
    const std::string &resource_label = resource_pair.first;
    const double &resource_capacity = resource_pair.second;
    RAY_CHECK(resource_capacity_.count(resource_label) == 1)
        << "Attempt to acquire unknown resource: " << resource_label;
    resource_capacity_[resource_label] -= resource_capacity;
    // TODO(romilb): Double precision subtraction may sometimes be less than zero by a
    // small epsilon - need to fix.
    RAY_CHECK(resource_capacity_[resource_label] >= 0 - EPSILON)
        << "Capacity of resource " << resource_label << " after subtraction is negative ("
        << resource_capacity_[resource_label] << ")."
        << " Debug: resource_capacity_:" << ToString() << ", other: " << other.ToString();
    if (EqualsZeroEpsilon(resource_capacity_[resource_label])) {
      resource_capacity_.erase(resource_label);
    }
  }
}

// Perform an outer join.
void ResourceSet::AddResources(const ResourceSet &other) {
  for (const auto &resource_pair : other.GetResourceMap()) {
    const std::string &resource_label = resource_pair.first;
    const double &resource_capacity = resource_pair.second;
    resource_capacity_[resource_label] += resource_capacity;
  }
}

double ResourceSet::GetResource(const std::string &resource_name) const {
  if (EqualsZeroEpsilon(resource_capacity_.count(resource_name))) {
    return 0;
  }
  double capacity = resource_capacity_.at(resource_name);
  RAY_CHECK(capacity > 0 + EPSILON) << "Resource " << resource_name << " capacity is "
                                    << capacity
                                    << ". Should have been greater than zero.";
  return capacity;
}

double ResourceSet::GetNumCpus() const { return GetResource(kCPU_ResourceLabel); }

const std::string ResourceSet::ToString() const {
  std::string return_string = "";

  auto it = resource_capacity_.begin();

  // Convert the first element to a string.
  if (it != resource_capacity_.end()) {
    return_string += "{" + it->first + "," + std::to_string(it->second) + "}";
    it++;
  }

  // Add the remaining elements to the string (along with a comma).
  for (; it != resource_capacity_.end(); ++it) {
    return_string += ",{" + it->first + "," + std::to_string(it->second) + "}";
  }

  return return_string;
}

const std::unordered_map<std::string, double> &ResourceSet::GetResourceMap() const {
  return resource_capacity_;
};

/// ResourceIds class implementation

ResourceIds::ResourceIds() {}

ResourceIds::ResourceIds(double resource_quantity) {
  RAY_CHECK(IsWhole(resource_quantity));
  int64_t whole_quantity = resource_quantity;
  for (int64_t i = 0; i < whole_quantity; ++i) {
    whole_ids_.push_back(i);
  }
}

ResourceIds::ResourceIds(const std::vector<int64_t> &whole_ids) : whole_ids_(whole_ids) {}

ResourceIds::ResourceIds(const std::vector<std::pair<int64_t, double>> &fractional_ids)
    : fractional_ids_(fractional_ids) {}

ResourceIds::ResourceIds(const std::vector<int64_t> &whole_ids,
                         const std::vector<std::pair<int64_t, double>> &fractional_ids)
    : whole_ids_(whole_ids), fractional_ids_(fractional_ids) {}

bool ResourceIds::Contains(double resource_quantity) const {
  RAY_CHECK(resource_quantity >= 0);
  if (resource_quantity >= 1) {
    RAY_CHECK(IsWhole(resource_quantity));
    return whole_ids_.size() >= resource_quantity;
  } else {
    if (whole_ids_.size() > 0) {
      return true;
    } else {
      for (auto const &fractional_pair : fractional_ids_) {
        if (fractional_pair.second >= resource_quantity) {
          return true;
        }
      }
      return false;
    }
  }
}

ResourceIds ResourceIds::Acquire(double resource_quantity) {
  RAY_CHECK(resource_quantity >= 0);
  if (resource_quantity >= 1) {
    // Handle the whole case.
    RAY_CHECK(IsWhole(resource_quantity));
    int64_t whole_quantity = resource_quantity;
    RAY_CHECK(static_cast<int64_t>(whole_ids_.size()) >= whole_quantity);

    std::vector<int64_t> ids_to_return;
    for (int64_t i = 0; i < whole_quantity; ++i) {
      ids_to_return.push_back(whole_ids_.back());
      whole_ids_.pop_back();
    }

    return ResourceIds(ids_to_return);

  } else {
    // Handle the fractional case.
    for (auto &fractional_pair : fractional_ids_) {
      if (fractional_pair.second >= resource_quantity) {
        auto return_pair = std::make_pair(fractional_pair.first, resource_quantity);
        fractional_pair.second -= resource_quantity;

        // Remove the fractional pair if the new capacity is 0
        if (EqualsZeroEpsilon(fractional_pair.second)) {
          std::swap(fractional_pair, fractional_ids_[fractional_ids_.size() - 1]);
          fractional_ids_.pop_back();
        }
        return ResourceIds({return_pair});
      }
    }

    // If we get here then there weren't enough available fractional IDs, so we
    // need to use a whole ID.
    RAY_CHECK(whole_ids_.size() > 0);
    int64_t whole_id = whole_ids_.back();
    whole_ids_.pop_back();

    auto return_pair = std::make_pair(whole_id, resource_quantity);
    fractional_ids_.push_back(std::make_pair(whole_id, 1 - resource_quantity));
    return ResourceIds({return_pair});
  }
}

void ResourceIds::Release(const ResourceIds &resource_ids) {
  auto const &whole_ids_to_return = resource_ids.WholeIds();

  // Return the whole IDs.
  whole_ids_.insert(whole_ids_.end(), whole_ids_to_return.begin(),
                    whole_ids_to_return.end());

  // Return the fractional IDs.
  auto const &fractional_ids_to_return = resource_ids.FractionalIds();
  for (auto const &fractional_pair_to_return : fractional_ids_to_return) {
    int64_t resource_id = fractional_pair_to_return.first;
    auto const &fractional_pair_it =
        std::find_if(fractional_ids_.begin(), fractional_ids_.end(),
                     [resource_id](std::pair<int64_t, double> &fractional_pair) {
                       return fractional_pair.first == resource_id;
                     });
    if (fractional_pair_it == fractional_ids_.end()) {
      fractional_ids_.push_back(fractional_pair_to_return);
    } else {
      fractional_pair_it->second += fractional_pair_to_return.second;
      // TODO(romilb): Double precision addition may sometimes exceed 1 by a small epsilon
      // - need to fix this.
      RAY_CHECK(fractional_pair_it->second <= 1 + EPSILON)
          << "Fractional Resource Id " << fractional_pair_it->first << " capacity is "
          << fractional_pair_it->second << ". Should have been less than one.";
      // If this makes the ID whole, then return it to the list of whole IDs.
      // TODO(romilb): Double precision addition may sometimes exceed 1 by a small epsilon
      // - need to fix this.
      if (EqualsOneEpsilon(fractional_pair_it->second)) {
        whole_ids_.push_back(resource_id);
        fractional_ids_.erase(fractional_pair_it);
      }
    }
  }
}

ResourceIds ResourceIds::Plus(const ResourceIds &resource_ids) const {
  ResourceIds resource_ids_to_return(whole_ids_, fractional_ids_);
  resource_ids_to_return.Release(resource_ids);
  return resource_ids_to_return;
}

const std::vector<int64_t> &ResourceIds::WholeIds() const { return whole_ids_; }

const std::vector<std::pair<int64_t, double>> &ResourceIds::FractionalIds() const {
  return fractional_ids_;
}

bool ResourceIds::TotalQuantityIsZero() const {
  return whole_ids_.empty() && fractional_ids_.empty();
}

double ResourceIds::TotalQuantity() const {
  double total_quantity = whole_ids_.size();
  for (auto const &fractional_pair : fractional_ids_) {
    total_quantity += fractional_pair.second;
  }
  return total_quantity;
}

std::string ResourceIds::ToString() const {
  std::string return_string = "Whole IDs: [";
  for (auto const &whole_id : whole_ids_) {
    return_string += std::to_string(whole_id) + ", ";
  }
  return_string += "], Fractional IDs: ";
  for (auto const &fractional_pair : fractional_ids_) {
    return_string += "(" + std::to_string(fractional_pair.first) + ", " +
                     std::to_string(fractional_pair.second) + "), ";
  }
  return_string += "]";
  return return_string;
}

bool ResourceIds::IsWhole(double resource_quantity) const {
  int64_t whole_quantity = resource_quantity;
  return whole_quantity == resource_quantity;
}

/// ResourceIdSet class implementation

ResourceIdSet::ResourceIdSet() {}

ResourceIdSet::ResourceIdSet(const ResourceSet &resource_set) {
  for (auto const &resource_pair : resource_set.GetResourceMap()) {
    auto const &resource_name = resource_pair.first;
    double resource_quantity = resource_pair.second;
    available_resources_[resource_name] = ResourceIds(resource_quantity);
  }
}

ResourceIdSet::ResourceIdSet(
    const std::unordered_map<std::string, ResourceIds> &available_resources)
    : available_resources_(available_resources) {}

bool ResourceIdSet::Contains(const ResourceSet &resource_set) const {
  for (auto const &resource_pair : resource_set.GetResourceMap()) {
    auto const &resource_name = resource_pair.first;
    double resource_quantity = resource_pair.second;
    RAY_CHECK(resource_quantity > 0 + EPSILON) << "Resource " << resource_name
                                               << " capacity is " << resource_quantity
                                               << ". Should have been greater than zero.";

    auto it = available_resources_.find(resource_name);
    if (it == available_resources_.end()) {
      return false;
    }

    if (!it->second.Contains(resource_quantity)) {
      return false;
    }
  }
  return true;
}

ResourceIdSet ResourceIdSet::Acquire(const ResourceSet &resource_set) {
  std::unordered_map<std::string, ResourceIds> acquired_resources;

  for (auto const &resource_pair : resource_set.GetResourceMap()) {
    auto const &resource_name = resource_pair.first;
    double resource_quantity = resource_pair.second;
    RAY_CHECK(resource_quantity > 0 + EPSILON) << "Resource " << resource_name
                                               << " capacity is " << resource_quantity
                                               << ". Should have been greater than zero.";

    auto it = available_resources_.find(resource_name);
    RAY_CHECK(it != available_resources_.end());
    acquired_resources[resource_name] = it->second.Acquire(resource_quantity);
    if (it->second.TotalQuantityIsZero()) {
      available_resources_.erase(it);
    }
  }
  return ResourceIdSet(acquired_resources);
}

void ResourceIdSet::Release(const ResourceIdSet &resource_id_set) {
  for (auto const &resource_pair : resource_id_set.AvailableResources()) {
    auto const &resource_name = resource_pair.first;
    auto const &resource_ids = resource_pair.second;
    RAY_CHECK(!resource_ids.TotalQuantityIsZero());

    auto it = available_resources_.find(resource_name);
    if (it == available_resources_.end()) {
      available_resources_[resource_name] = resource_ids;
    } else {
      it->second.Release(resource_ids);
    }
  }
}

void ResourceIdSet::Clear() { available_resources_.clear(); }

ResourceIdSet ResourceIdSet::Plus(const ResourceIdSet &resource_id_set) const {
  ResourceIdSet resource_id_set_to_return(available_resources_);
  resource_id_set_to_return.Release(resource_id_set);
  return resource_id_set_to_return;
}

const std::unordered_map<std::string, ResourceIds> &ResourceIdSet::AvailableResources()
    const {
  return available_resources_;
}

ResourceIdSet ResourceIdSet::GetCpuResources() const {
  std::unordered_map<std::string, ResourceIds> cpu_resources;

  auto it = available_resources_.find(kCPU_ResourceLabel);
  if (it != available_resources_.end()) {
    cpu_resources.insert(*it);
  }
  return ResourceIdSet(cpu_resources);
}

ResourceSet ResourceIdSet::ToResourceSet() const {
  std::unordered_map<std::string, double> resource_set;
  for (auto const &resource_pair : available_resources_) {
    resource_set[resource_pair.first] = resource_pair.second.TotalQuantity();
  }
  return ResourceSet(resource_set);
}

std::string ResourceIdSet::ToString() const {
  std::string return_string = "AvailableResources: ";

  auto it = available_resources_.begin();

  // Convert the first element to a string.
  if (it != available_resources_.end()) {
    return_string += (it->first + ": {" + it->second.ToString() + "}");
  }
  it++;

  // Add the remaining elements to the string (along with a comma).
  for (; it != available_resources_.end(); ++it) {
    return_string += (", " + it->first + ": {" + it->second.ToString() + "}");
  }

  return return_string;
}

std::vector<flatbuffers::Offset<protocol::ResourceIdSetInfo>> ResourceIdSet::ToFlatbuf(
    flatbuffers::FlatBufferBuilder &fbb) const {
  std::vector<flatbuffers::Offset<protocol::ResourceIdSetInfo>> return_message;
  for (auto const &resource_pair : available_resources_) {
    std::vector<int64_t> resource_ids;
    std::vector<double> resource_fractions;
    for (auto whole_id : resource_pair.second.WholeIds()) {
      resource_ids.push_back(whole_id);
      resource_fractions.push_back(1);
    }

    for (auto const &fractional_pair : resource_pair.second.FractionalIds()) {
      resource_ids.push_back(fractional_pair.first);
      resource_fractions.push_back(fractional_pair.second);
    }

    auto resource_id_set_message = protocol::CreateResourceIdSetInfo(
        fbb, fbb.CreateString(resource_pair.first), fbb.CreateVector(resource_ids),
        fbb.CreateVector(resource_fractions));

    return_message.push_back(resource_id_set_message);
  }

  return return_message;
}

/// SchedulingResources class implementation

SchedulingResources::SchedulingResources()
    : resources_total_(ResourceSet()),
      resources_available_(ResourceSet()),
      resources_load_(ResourceSet()) {}

SchedulingResources::SchedulingResources(const ResourceSet &total)
    : resources_total_(total),
      resources_available_(total),
      resources_load_(ResourceSet()) {}

SchedulingResources::~SchedulingResources() {}

const ResourceSet &SchedulingResources::GetAvailableResources() const {
  return resources_available_;
}

void SchedulingResources::SetAvailableResources(ResourceSet &&newset) {
  resources_available_ = newset;
}

const ResourceSet &SchedulingResources::GetTotalResources() const {
  return resources_total_;
}

void SchedulingResources::SetLoadResources(ResourceSet &&newset) {
  resources_load_ = newset;
}

const ResourceSet &SchedulingResources::GetLoadResources() const {
  return resources_load_;
}

// Return specified resources back to SchedulingResources.
void SchedulingResources::Release(const ResourceSet &resources) {
  resources_available_.AddResources(resources);
}

// Take specified resources from SchedulingResources.
void SchedulingResources::Acquire(const ResourceSet &resources) {
  resources_available_.SubtractResourcesStrict(resources);
}

std::string SchedulingResources::DebugString() const {
  std::stringstream result;
  result << "\n- total: " << resources_total_.ToString();
  result << "\n- avail: " << resources_available_.ToString();
  return result.str();
};

}  // namespace raylet

}  // namespace ray
