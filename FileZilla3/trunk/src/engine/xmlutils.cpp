#include <filezilla.h>
#include "xmlutils.h"

#include <cstring>
#include <cassert>

pugi::xml_node AddTextElement(pugi::xml_node node, const char* name, std::string const& value, bool overwrite)
{
	return AddTextElementUtf8(node, name, fz::to_utf8(value), overwrite);
}

pugi::xml_node AddTextElement(pugi::xml_node node, const char* name, std::wstring const& value, bool overwrite)
{
	return AddTextElementUtf8(node, name, fz::to_utf8(value), overwrite);
}

void AddTextElement(pugi::xml_node node, const char* name, int64_t value, bool overwrite)
{
	if (overwrite) {
		node.remove_child(name);
	}
	auto child = node.append_child(name);
	child.text().set(static_cast<long long>(value));
}

pugi::xml_node AddTextElementUtf8(pugi::xml_node node, const char* name, std::string const& value, bool overwrite)
{
	assert(node);

	if (overwrite) {
		node.remove_child(name);
	}

	auto element = node.append_child(name);
	if (!value.empty()) {
		element.text().set(value.c_str());
	}

	return element;
}

void AddTextElement(pugi::xml_node node, std::string const& value)
{
	AddTextElementUtf8(node, fz::to_utf8(value));
}

void AddTextElement(pugi::xml_node node, std::wstring const& value)
{
	AddTextElementUtf8(node, fz::to_utf8(value));
}

void AddTextElement(pugi::xml_node node, int64_t value)
{
	assert(node);
	node.text().set(static_cast<long long>(value));
}

void AddTextElementUtf8(pugi::xml_node node, std::string const& value)
{
	assert(node);
	node.text().set(value.c_str());
}

std::wstring GetTextElement_Trimmed(pugi::xml_node node, const char* name)
{
	return fz::trimmed(GetTextElement(node, name));
}

std::wstring GetTextElement(pugi::xml_node node, const char* name)
{
	assert(node);

	return fz::to_wstring_from_utf8(node.child_value(name));
}

std::wstring GetTextElement_Trimmed(pugi::xml_node node)
{
	return fz::trimmed(GetTextElement(node));
}

std::wstring GetTextElement(pugi::xml_node node)
{
	assert(node);
	return fz::to_wstring_from_utf8(node.child_value());
}

int64_t GetTextElementInt(pugi::xml_node node, const char* name, int defValue /*=0*/)
{
	assert(node);
	return static_cast<int64_t>(node.child(name).text().as_llong(defValue));
}

bool GetTextElementBool(pugi::xml_node node, const char* name, bool defValue /*=false*/)
{
	assert(node);
	return node.child(name).text().as_bool(defValue);
}

void SetTextAttribute(pugi::xml_node node, char const* name, std::string const& value)
{
	SetTextAttributeUtf8(node, name, fz::to_utf8(value));
}

void SetTextAttribute(pugi::xml_node node, char const* name, std::wstring const& value)
{
	SetTextAttributeUtf8(node, name, fz::to_utf8(value));
}

void SetTextAttributeUtf8(pugi::xml_node node, char const* name, std::string const& utf8)
{
	assert(node);
	auto attribute = node.attribute(name);
	if (!attribute) {
		attribute = node.append_attribute(name);
	}
	attribute.set_value(utf8.c_str());
}

std::wstring GetTextAttribute(pugi::xml_node node, char const* name)
{
	assert(node);

	const char* value = node.attribute(name).value();
	return fz::to_wstring_from_utf8(value);
}

pugi::xml_node FindElementWithAttribute(pugi::xml_node node, const char* element, const char* attribute, const char* value)
{
	pugi::xml_node child = element ? node.child(element) : node.first_child();
	while (child) {
		const char* nodeVal = child.attribute(attribute).value();
		if (nodeVal && !strcmp(value, nodeVal)) {
			return child;
		}

		child = element ? child.next_sibling(element) : child.next_sibling();
	}

	return child;
}

int GetAttributeInt(pugi::xml_node node, const char* name)
{
	return node.attribute(name).as_int();
}

void SetAttributeInt(pugi::xml_node node, const char* name, int value)
{
	auto attribute = node.attribute(name);
	if (!attribute) {
		attribute = node.append_attribute(name);
	}
	attribute.set_value(value);
}
