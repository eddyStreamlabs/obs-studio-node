
#include "obs-property.hpp"

std::shared_ptr<obs::Property> obs::Property::deserialize(std::vector<char> const& buf)
{
	obs::Property::Type type = (Type)buf[0];

	std::shared_ptr<Property> prop;
	switch (type) {
	case Type::Invalid:
		return nullptr;
		break;
	case Type::Boolean:
		prop = std::make_shared<BooleanProperty>();
		break;
	case Type::Integer:
		prop = std::make_shared<IntegerProperty>();
		break;
	case Type::Float:
		prop = std::make_shared<FloatProperty>();
		break;
	case Type::Text:
		prop = std::make_shared<TextProperty>();
		break;
	case Type::Path:
		prop = std::make_shared<PathProperty>();
		break;
	case Type::List:
		prop = std::make_shared<ListProperty>();
		break;
	case Type::Color:
		prop = std::make_shared<ColorProperty>();
		break;
	case Type::Button:
		prop = std::make_shared<ButtonProperty>();
		break;
	case Type::Font:
		prop = std::make_shared<FontProperty>();
		break;
	case Type::EditableList:
		prop = std::make_shared<EditableListProperty>();
		break;
	case Type::FrameRate:
		prop = std::make_shared<FrameRateProperty>();
		break;
	}
	if (!prop) {
		return nullptr;
	}
	if (!prop->read(buf)) {
		return nullptr;
	}
	return prop;
}

obs::Property::Type obs::Property::type()
{
	return Type::Invalid;
}

size_t obs::Property::size()
{
	size_t total = 0;
	total += sizeof(Type);
	total += sizeof(size_t);
	total += name.size();
	total += sizeof(size_t);
	total += description.size();
	total += sizeof(size_t);
	total += long_description.size();
	total += sizeof(uint8_t) * 2; // enabled, visible
	return total;
}

bool obs::Property::serialize(std::vector<char>& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	size_t offset = 0;
	buf[offset]   = uint8_t(type());
	offset++;

	reinterpret_cast<size_t&>(buf[offset]) = name.size();
	offset += sizeof(size_t);
	std::memcpy(&buf[offset], name.data(), name.size());
	offset += name.size();

	reinterpret_cast<size_t&>(buf[offset]) = description.size();
	offset += sizeof(size_t);
	std::memcpy(&buf[offset], description.data(), description.size());
	offset += description.size();

	reinterpret_cast<size_t&>(buf[offset]) = long_description.size();
	offset += sizeof(size_t);
	std::memcpy(&buf[offset], long_description.data(), long_description.size());
	offset += long_description.size();

	buf[offset] = enabled;
	offset++;
	buf[offset] = visible;
	offset++;

	return true;
}

bool obs::Property::read(std::vector<char> const& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	size_t offset = 0;

	size_t length = 0;

	/* Type is already read by deserialize(). */ offset += 1;

	length = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	name = std::string(&buf[offset], length);
	offset += length;

	length = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	description = std::string(&buf[offset], length);
	offset += length;

	length = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	long_description = std::string(&buf[offset], length);
	offset += length;

	enabled = !!buf[offset];
	offset += sizeof(uint8_t);
	visible = !!buf[offset];
	offset += sizeof(uint8_t);

	return true;
}

obs::Property::Type obs::BooleanProperty::type()
{
	return obs::Property::Type::Boolean;
}

size_t obs::BooleanProperty::size()
{
	return Property::size();
}

bool obs::BooleanProperty::read(std::vector<char> const& buf)
{
	return Property::read(buf);
}

bool obs::BooleanProperty::serialize(std::vector<char>& buf)
{
	return Property::serialize(buf);
}

size_t obs::NumberProperty::size()
{
	size_t total = Property::size();
	total += sizeof(uint8_t);
	return total;
}

bool obs::NumberProperty::serialize(std::vector<char>& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::serialize(buf)) {
		return false;
	}

	size_t offset = Property::size();
	buf[offset]   = uint8_t(field_type);

	return true;
}

bool obs::NumberProperty::read(std::vector<char> const& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::read(buf)) {
		return false;
	}

	size_t offset = Property::size();
	field_type    = NumberType(buf[offset]);

	return true;
}

obs::Property::Type obs::IntegerProperty::type()
{
	return Type::Integer;
}

size_t obs::IntegerProperty::size()
{
	size_t total = NumberProperty::size();
	total += sizeof(int64_t) * 3;
	return total;
}

bool obs::IntegerProperty::serialize(std::vector<char>& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!NumberProperty::serialize(buf)) {
		return false;
	}

	size_t offset                             = NumberProperty::size();
	*reinterpret_cast<int64_t*>(&buf[offset]) = minimum;
	offset += sizeof(int64_t);
	*reinterpret_cast<int64_t*>(&buf[offset]) = maximum;
	offset += sizeof(int64_t);
	*reinterpret_cast<int64_t*>(&buf[offset]) = step;
	offset += sizeof(int64_t);

	return true;
}

bool obs::IntegerProperty::read(std::vector<char> const& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!NumberProperty::read(buf)) {
		return false;
	}

	size_t offset = NumberProperty::size();
	minimum       = *reinterpret_cast<const int64_t*>(&buf[offset]);
	offset += sizeof(int64_t);
	maximum = *reinterpret_cast<const int64_t*>(&buf[offset]);
	offset += sizeof(int64_t);
	step = *reinterpret_cast<const int64_t*>(&buf[offset]);
	offset += sizeof(int64_t);

	return true;
}

obs::Property::Type obs::FloatProperty::type()
{
	return Type::Float;
}

size_t obs::FloatProperty::size()
{
	size_t total = NumberProperty::size();
	total += sizeof(double) * 3;
	return total;
}

bool obs::FloatProperty::serialize(std::vector<char>& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!NumberProperty::serialize(buf)) {
		return false;
	}

	size_t offset                              = NumberProperty::size();
	*reinterpret_cast<double*>(&buf[offset]) = minimum;
	offset += sizeof(double);
	*reinterpret_cast<double*>(&buf[offset]) = maximum;
	offset += sizeof(double);
	*reinterpret_cast<double*>(&buf[offset]) = step;
	offset += sizeof(double);

	return true;
}

bool obs::FloatProperty::read(std::vector<char> const& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!NumberProperty::read(buf)) {
		return false;
	}

	size_t offset = NumberProperty::size();
	minimum       = *reinterpret_cast<const double*>(&buf[offset]);
	offset += sizeof(double);
	maximum = *reinterpret_cast<const double*>(&buf[offset]);
	offset += sizeof(double);
	step = *reinterpret_cast<const double*>(&buf[offset]);
	offset += sizeof(double);

	return true;
}

obs::Property::Type obs::TextProperty::type()
{
	return Type::Text;
}

size_t obs::TextProperty::size()
{
	size_t total = Property::size();
	total += sizeof(uint8_t);
	return total;
}

bool obs::TextProperty::serialize(std::vector<char>& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::serialize(buf)) {
		return false;
	}

	size_t offset = Property::size();
	buf[offset]   = uint8_t(field_type);

	return true;
}

bool obs::TextProperty::read(std::vector<char> const& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::read(buf)) {
		return false;
	}

	size_t offset = Property::size();
	field_type    = TextType(buf[offset]);

	return true;
}

obs::Property::Type obs::PathProperty::type()
{
	return Type::Path;
}

size_t obs::PathProperty::size()
{
	size_t total = Property::size();
	total += sizeof(uint8_t);
	total += sizeof(size_t) + filter.size();
	total += sizeof(size_t) + default_path.size();
	return total;
}

bool obs::PathProperty::serialize(std::vector<char>& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::serialize(buf)) {
		return false;
	}

	size_t offset = Property::size();
	buf[offset]   = uint8_t(field_type);
	offset += sizeof(uint8_t);

	reinterpret_cast<size_t&>(buf[offset]) = filter.size();
	offset += sizeof(size_t);
	if (filter.size() > 0) {
		memcpy(&buf[offset], filter.data(), filter.size());
		offset += filter.size();
	}

	reinterpret_cast<size_t&>(buf[offset]) = default_path.size();
	offset += sizeof(size_t);
	if (default_path.size() > 0) {
		memcpy(&buf[offset], default_path.data(), default_path.size());
		offset += default_path.size();
	}

	return true;
}

bool obs::PathProperty::read(std::vector<char> const& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::read(buf)) {
		return false;
	}

	size_t length = 0;

	size_t offset = Property::size();
	field_type    = PathType(buf[offset]);
	offset += sizeof(uint8_t);

	length = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	if (length > 0) {
		filter = std::string(&buf[offset], length);
		offset += length;
	}

	length = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	if (length > 0) {
		default_path = std::string(&buf[offset], length);
		offset += length;
	}

	return true;
}

obs::Property::Type obs::ListProperty::type()
{
	return Type::List;
}

size_t obs::ListProperty::size()
{
	size_t total = Property::size();
	total += sizeof(uint8_t);
	total += sizeof(uint8_t);
	total += sizeof(size_t);
	for (auto& entry : items) {
		total += sizeof(size_t);
		total += entry.name.size();
		total += sizeof(uint8_t);
		switch (format) {
		case Format::Integer:
			total += sizeof(int64_t);
			break;
		case Format::Float:
			total += sizeof(double);
			break;
		case Format::String:
			total += sizeof(size_t);
			total += entry.value_string.size();
			break;
		}
	}
	return total;
}

bool obs::ListProperty::serialize(std::vector<char>& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::serialize(buf)) {
		return false;
	}

	size_t offset = Property::size();
	buf[offset]   = uint8_t(field_type);
	offset += sizeof(uint8_t);
	buf[offset] = uint8_t(format);
	offset += sizeof(uint8_t);

	reinterpret_cast<size_t&>(buf[offset]) = items.size();
	offset += sizeof(size_t);
	for (auto& entry : items) {
		reinterpret_cast<size_t&>(buf[offset]) = entry.name.size();
		offset += sizeof(size_t);
		if (entry.name.size() > 0) {
			memcpy(&buf[offset], entry.name.data(), entry.name.size());
			offset += entry.name.size();
		}
		buf[offset] = enabled;
		offset += sizeof(uint8_t);
		switch (format) {
		case Format::Integer:
			reinterpret_cast<int64_t&>(buf[offset]) = entry.value_int;
			offset += sizeof(int64_t);
			break;
		case Format::Float:
			reinterpret_cast<double&>(buf[offset]) = entry.value_float;
			offset += sizeof(double);
			break;
		case Format::String:
			reinterpret_cast<size_t&>(buf[offset]) = entry.value_string.size();
			offset += sizeof(size_t);
			if (entry.value_string.size() > 0) {
				memcpy(&buf[offset], entry.value_string.data(), entry.value_string.size());
				offset += entry.value_string.size();
			}
			break;
		}
	}

	return true;
}

bool obs::ListProperty::read(std::vector<char> const& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::read(buf)) {
		return false;
	}

	size_t offset = Property::size();
	field_type    = ListType(buf[offset]);
	offset += sizeof(uint8_t);
	format = Format(buf[offset]);
	offset += sizeof(uint8_t);

	size_t num_entries = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	for (size_t idx = 0; idx < num_entries; idx++) {
		size_t length = 0;
		Item   entry;

		length = reinterpret_cast<const size_t&>(buf[offset]);
		offset += sizeof(size_t);
		if (length > 0) {
			entry.name = std::string(&buf[offset], length);
			offset += length;
		}
		entry.enabled = !!buf[offset];
		offset += sizeof(uint8_t);
		switch (format) {
		case Format::Integer:
			entry.value_int = reinterpret_cast<const int64_t&>(buf[offset]);
			offset += sizeof(int64_t);
			break;
		case Format::Float:
			entry.value_float = reinterpret_cast<const double&>(buf[offset]);
			offset += sizeof(double);
			break;
		case Format::String:
			length = reinterpret_cast<const size_t&>(buf[offset]);
			offset += sizeof(size_t);
			if (length > 0) {
				entry.value_string = std::string(&buf[offset], length);
				offset += length;
			}
			break;
		}
		items.push_back(std::move(entry));
	}

	return true;
}

obs::Property::Type obs::ColorProperty::type()
{
	return obs::Property::Type::Color;
}

size_t obs::ColorProperty::size()
{
	return Property::size();
}

bool obs::ColorProperty::serialize(std::vector<char>& buf)
{
	return Property::serialize(buf);
}

bool obs::ColorProperty::read(std::vector<char> const& buf)
{
	return Property::read(buf);
}

obs::Property::Type obs::ButtonProperty::type()
{
	return obs::Property::Type::Button;
}

size_t obs::ButtonProperty::size()
{
	return Property::size();
}

bool obs::ButtonProperty::serialize(std::vector<char>& buf)
{
	return Property::serialize(buf);
}

bool obs::ButtonProperty::read(std::vector<char> const& buf)
{
	return Property::read(buf);
}

obs::Property::Type obs::FontProperty::type()
{
	return obs::Property::Type::Font;
}

size_t obs::FontProperty::size()
{
	return Property::size();
}

bool obs::FontProperty::serialize(std::vector<char>& buf)
{
	return Property::serialize(buf);
}

bool obs::FontProperty::read(std::vector<char> const& buf)
{
	return Property::read(buf);
}

obs::Property::Type obs::EditableListProperty::type()
{
	return Type::EditableList;
}

size_t obs::EditableListProperty::size()
{
	size_t total = Property::size();
	total += sizeof(uint8_t);
	total += sizeof(size_t) + filter.size();
	total += sizeof(size_t) + default_path.size();
	return total;
}

bool obs::EditableListProperty::serialize(std::vector<char>& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::serialize(buf)) {
		return false;
	}

	size_t offset = Property::size();
	buf[offset]   = uint8_t(field_type);
	offset += sizeof(uint8_t);

	reinterpret_cast<size_t&>(buf[offset]) = filter.size();
	offset += sizeof(size_t);
	if (filter.size() > 0) {
		memcpy(&buf[offset], filter.data(), filter.size());
		offset += filter.size();
	}

	reinterpret_cast<size_t&>(buf[offset]) = default_path.size();
	offset += sizeof(size_t);
	if (default_path.size() > 0) {
		memcpy(&buf[offset], default_path.data(), default_path.size());
		offset += default_path.size();
	}

	return true;
}

bool obs::EditableListProperty::read(std::vector<char> const& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::read(buf)) {
		return false;
	}

	size_t length = 0;

	size_t offset = Property::size();
	field_type    = ListType(buf[offset]);
	offset += sizeof(uint8_t);

	length = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	if (length > 0) {
		filter = std::string(&buf[offset], length);
		offset += length;
	}

	length = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	if (length > 0) {
		default_path = std::string(&buf[offset], length);
		offset += length;
	}

	return true;
}

obs::Property::Type obs::FrameRateProperty::type()
{
	return Type::FrameRate;
}

size_t obs::FrameRateProperty::size()
{
	size_t total = Property::size();
	total += sizeof(size_t);
	total += (sizeof(uint32_t) * 4) * ranges.size();
	total += sizeof(size_t);
	for (Option& option : options) {
		total += sizeof(size_t);
		total += option.name.size();
		total += sizeof(size_t);
		total += option.description.size();
	}
	return total;
}

bool obs::FrameRateProperty::serialize(std::vector<char>& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::serialize(buf)) {
		return false;
	}

	size_t offset = Property::size();

	reinterpret_cast<size_t&>(buf[offset]) = ranges.size();
	offset += sizeof(size_t);
	for (Range& range : ranges) {
		reinterpret_cast<uint32_t&>(buf[offset]) = range.minimum.first;
		offset += sizeof(uint32_t);
		reinterpret_cast<uint32_t&>(buf[offset]) = range.minimum.second;
		offset += sizeof(uint32_t);
		reinterpret_cast<uint32_t&>(buf[offset]) = range.maximum.first;
		offset += sizeof(uint32_t);
		reinterpret_cast<uint32_t&>(buf[offset]) = range.maximum.second;
		offset += sizeof(uint32_t);
	}

	reinterpret_cast<size_t&>(buf[offset]) = options.size();
	offset += sizeof(size_t);
	for (Option& option : options) {
		reinterpret_cast<size_t&>(buf[offset]) = option.name.size();
		offset += sizeof(size_t);
		if (option.name.size() > 0) {
			memcpy(&buf[offset], option.name.data(), option.name.size());
			offset += option.name.size();
		}
		reinterpret_cast<size_t&>(buf[offset]) = option.description.size();
		offset += sizeof(size_t);
		if (option.description.size() > 0) {
			memcpy(&buf[offset], option.description.data(), option.description.size());
			offset += option.description.size();
		}
	}

	return true;
}

bool obs::FrameRateProperty::read(std::vector<char> const& buf)
{
	if (buf.size() < size()) {
		return false;
	}

	if (!Property::read(buf)) {
		return false;
	}

	size_t length = 0;

	size_t offset     = Property::size();
	size_t num_ranges = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	for (size_t idx = 0; idx < num_ranges; idx++) {
		Range range;
		range.minimum.first = reinterpret_cast<const uint32_t&>(buf[offset]);
		offset += sizeof(uint32_t);
		range.minimum.second = reinterpret_cast<const uint32_t&>(buf[offset]);
		offset += sizeof(uint32_t);
		range.maximum.first = reinterpret_cast<const uint32_t&>(buf[offset]);
		offset += sizeof(uint32_t);
		range.maximum.second = reinterpret_cast<const uint32_t&>(buf[offset]);
		offset += sizeof(uint32_t);
		ranges.push_back(std::move(range));
	}

	size_t num_options = reinterpret_cast<const size_t&>(buf[offset]);
	offset += sizeof(size_t);
	for (size_t idx = 0; idx < num_options; idx++) {
		Option option;
		length = reinterpret_cast<const size_t&>(buf[offset]);
		offset += sizeof(size_t);
		if (length > 0) {
			option.name = std::string(&buf[offset], length);
		}
		length = reinterpret_cast<const size_t&>(buf[offset]);
		offset += sizeof(size_t);
		if (length > 0) {
			option.description = std::string(&buf[offset], length);
		}
		options.push_back(std::move(option));
	}

	return true;
}
