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
function isNumberProperty(property) {
    return property.type === 2 ||
        property.type === 3;
}
function isTextProperty(property) {
    return property.type === 4;
}
function isPathProperty(property) {
    return property.type === 5;
}
function isListProperty(property) {
    return property.type === 6;
}
function isEditableListProperty(property) {
    return property.type === 10;
}
function isBooleanProperty(property) {
    return property.type === 1;
}
function isButtonProperty(property) {
    return property.type === 8;
}
function isColorProperty(property) {
    return property.type === 7;
}
function isCaptureProperty(property) {
    return property.type === 14;
}
function isFontProperty(property) {
    return property.type === 9;
}
function isEmptyProperty(property) {
    switch (property.type) {
        case 1:
        case 8:
        case 7:
        case 9:
        case 0:
            return true;
    }
    return false;
}
