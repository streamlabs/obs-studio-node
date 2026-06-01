"use strict";
Object.defineProperty(exports, "__esModule", { value: true });
exports.isNumberProperty = isNumberProperty;
exports.isTextProperty = isTextProperty;
exports.isPathProperty = isPathProperty;
exports.isListProperty = isListProperty;
exports.isEditableListProperty = isEditableListProperty;
exports.isBooleanProperty = isBooleanProperty;
exports.isButtonProperty = isButtonProperty;
exports.isColorProperty = isColorProperty;
exports.isCaptureProperty = isCaptureProperty;
exports.isFontProperty = isFontProperty;
exports.isEmptyProperty = isEmptyProperty;
const obs = require("./module");
function isNumberProperty(property) {
    return property.type === obs.EPropertyType.Int ||
        property.type === obs.EPropertyType.Float;
}
function isTextProperty(property) {
    return property.type === obs.EPropertyType.Text;
}
function isPathProperty(property) {
    return property.type === obs.EPropertyType.Path;
}
function isListProperty(property) {
    return property.type === obs.EPropertyType.List;
}
function isEditableListProperty(property) {
    return property.type === obs.EPropertyType.EditableList;
}
function isBooleanProperty(property) {
    return property.type === obs.EPropertyType.Boolean;
}
function isButtonProperty(property) {
    return property.type === obs.EPropertyType.Button;
}
function isColorProperty(property) {
    return property.type === obs.EPropertyType.Color;
}
function isCaptureProperty(property) {
    return property.type === obs.EPropertyType.Capture;
}
function isFontProperty(property) {
    return property.type === obs.EPropertyType.Font;
}
function isEmptyProperty(property) {
    switch (property.type) {
        case obs.EPropertyType.Boolean:
        case obs.EPropertyType.Button:
        case obs.EPropertyType.Color:
        case obs.EPropertyType.Font:
        case obs.EPropertyType.Invalid:
            return true;
    }
    return false;
}
