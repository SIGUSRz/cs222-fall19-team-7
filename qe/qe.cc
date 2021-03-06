
#include "qe.h"

Filter::Filter(Iterator *input, const Condition &condition) {
    this->input = input;
    this->condition = condition;
    input->getAttributes(this->attrs);
    // Drop the relation name
    for (int i = 0; i < this->attrs.size(); i++) {
        int pos = this->attrs[i].name.find('.');
        string attrName = this->attrs[i].name.substr(pos + 1, this->attrs[i].name.length() - pos + 1);
        this->attrs[i].name = attrName;
    }
}

RC Filter::getNextTuple(void *data) {
    while (input->getNextTuple(data) != RM_EOF) {
        void *filterValue = malloc(PAGE_SIZE);
        int pos = getAttrValue(this->attrs, this->condition.lhsAttr, filterValue, data);
        if (pos == -1) {
            // Invalid tuple
            return -1;
        }
        bool satisfied = compare(filterValue, condition.rhsValue);
        if (satisfied) {
            return 0;
        }
    }
    return RM_EOF;
}

bool Filter::compare(void *filterValue, Value rhsValue) {
    bool satisfied = false;
    Attribute attr;
    attr.type = this->condition.rhsValue.type;
    switch (condition.op) {
        case EQ_OP: {
            bool rc = compareEqual(attr, filterValue, rhsValue.data);
            if (rc == true) {
                satisfied = true;
            }
        }
            break;
        case LT_OP: {
            int rc = compareLess(attr, filterValue, rhsValue.data);
            if (rc == 1) {
                satisfied = true;
            }
        }
            break;
        case LE_OP: {
            int rc = compareLess(attr, filterValue, rhsValue.data);
            if (rc >= 0) {
                satisfied = true;
            }
        }
            break;
        case GT_OP: {
            int rc = compareLarge(attr, filterValue, rhsValue.data);
            if (rc == 1) {
                satisfied = true;
            }
        }
            break;
        case GE_OP: {
            int rc = compareLarge(attr, filterValue, rhsValue.data);
            if (rc >= 0) {
                satisfied = true;
            }
        }
            break;
        case NE_OP: {
            bool rc = compareEqual(attr, filterValue, rhsValue.data);
            if (rc != true) {
                satisfied = true;
            }
            break;
        }
        case NO_OP:
            satisfied = true;
            break;
    }
    return satisfied;
}

void Filter::getAttributes(std::vector<Attribute> &attrs) const {
    attrs.clear();
    attrs = this->attrs;
    for (int i = 0; i < this->attrs.size(); i++) {
        string attrName = "Filter." + this->attrs[i].name;
        attrs[i].name = attrName;
    }
}

int getAttrValue(vector<Attribute> attrs, string attrName, void *filterValue, void *data) {
    int index = -1;
    int nullIndicatorSize = ceil((double) attrs.size() / CHAR_BIT);
    int offset = nullIndicatorSize;
    char *nullIndicator = (char *) malloc(nullIndicatorSize);
    memcpy(nullIndicator, data, nullIndicatorSize);
    int pos = attrName.find('.');
    string lhsAttrName = attrName.substr(pos + 1, attrName.length() - pos + 1);
    int i;
    for (i = 0; i < attrs.size(); i++) {
        bool isNull = nullIndicator[i / CHAR_BIT] & (1 << (7 - i % CHAR_BIT));
        if (isNull) {
            continue;
        }
        if (lhsAttrName == attrs[i].name) {
            index = i;
            if (attrs[i].type == TypeInt || attrs[i].type == TypeReal) {
                memcpy(filterValue, (char *) data + offset, sizeof(int));
            } else {
                int localLength;
                memcpy(&localLength, (char *) data + offset, sizeof(int));
                offset += sizeof(int);
                memcpy(filterValue, &localLength, sizeof(int));
                memcpy((char *) filterValue + sizeof(int), (char *) data + offset, localLength);
            }
            return index;
        } else {
            if (attrs[i].type == TypeInt || attrs[i].type == TypeReal) {
                offset += sizeof(int);
            } else {
                int length;
                memcpy(&length, (char *) data + offset, sizeof(int));
                offset += sizeof(int) + length;
            }
        }
    }
    return index;
}

Project::Project(Iterator *input, const std::vector<std::string> &attrNames) {
    this->input = input;
    input->getAttributes(this->attrs);
    for (int i = 0; i < this->attrs.size(); i++) {
        int pos = this->attrs[i].name.find('.');
        string attrName = this->attrs[i].name.substr(pos + 1, this->attrs[i].name.length() - pos + 1);
        this->attrs[i].name = attrName;
    }

    for (int i = 0; i < attrNames.size(); i++) {
        int pos = attrNames[i].find('.');
        string attrName = attrNames[i].substr(pos + 1, attrNames[i].length() - pos + 1);
        for (int j = 0; j < this->attrs.size(); j++) {
            if (attrName == this->attrs[j].name) {
                this->projectAttrs.push_back(this->attrs[j]);
            }
        }
    }
}

void Project::getAttributes(std::vector<Attribute> &attrs) const {
    attrs = this->projectAttrs;
    for (int i = 0; i < this->projectAttrs.size(); i++) {
        string attrName = "Project." + this->projectAttrs[i].name;
        attrs[i].name = attrName;
    }
}

RC Project::getNextTuple(void *data) {
    void *page = malloc(PAGE_SIZE);
    while (this->input->getNextTuple(page) != RM_EOF) {
        int length = this->getProjectValue(data, page);
        if (length == -1) {
            continue;
        }
        free(page);
        return 0;
    }
    return RM_EOF;
}

int Project::getProjectValue(void *projectDataValue, void *page) {
    int count = 0;
    int nullIndicatorSize = ceil((double) this->attrs.size() / CHAR_BIT);
    char *nullIndicator = (char *) malloc(nullIndicatorSize);
    memcpy(nullIndicator, page, nullIndicatorSize);
    int projectNullIndicatorSize = ceil((double) this->projectAttrs.size() / CHAR_BIT);
    char *projectNullIndicator = (char *) malloc(projectNullIndicatorSize);
    memset(projectNullIndicator, 0, projectNullIndicatorSize);
    int projectOffset = projectNullIndicatorSize;
    for (int i = 0; i < this->projectAttrs.size(); i++) {
        int offset = nullIndicatorSize;
        for (int j = 0; j < this->attrs.size(); j++) {
            bool isNull = nullIndicator[j / CHAR_BIT] & (1 << (7 - j % CHAR_BIT));
            if (this->attrs[j].name == this->projectAttrs[i].name) {
                count++;
                if (isNull) {
                    projectNullIndicator[i / 8] || (1 << (7 - i % 8));
                }
                if (this->attrs[j].type == TypeInt || this->attrs[j].type == TypeReal) {
                    memcpy((char *) projectDataValue + projectOffset, (char *) page + offset, sizeof(int));
                    offset += sizeof(int);
                    projectOffset += sizeof(int);
                } else {
                    int localLength;
                    memcpy(&localLength, (char *) page + 5, sizeof(int));
                    memcpy((char *) projectDataValue + projectOffset, &localLength, sizeof(int));
                    offset += sizeof(int);
                    projectOffset += sizeof(int);
                    memcpy((char *) projectDataValue + projectNullIndicatorSize, (char *) page + offset, localLength);
                    offset += localLength;
                    projectOffset += localLength;
                }
                break;
            } else if (!isNull) {
                if (this->attrs[j].type == TypeInt || this->attrs[j].type == TypeReal) {
                    offset += sizeof(int);
                } else {
                    int localLength;
                    memcpy(&localLength, (char *) page + offset, sizeof(int));
                    offset += sizeof(int) + localLength;
                }
            }
        }
    }
    if (count < this->projectAttrs.size()) {
        // Some project attributes are missing, report error
        return -1;
    }
    memcpy(projectDataValue, projectNullIndicator, projectNullIndicatorSize);
    return projectOffset;
}

BNLJoin::BNLJoin(Iterator *leftIn, TableScan *rightIn, const Condition &condition, const unsigned numPages) {
    this->leftIn = leftIn;
    this->rightIn = rightIn;
    leftIn->getAttributes(this->leftAttrs);
    rightIn->getAttributes(this->rightAttrs);
    this->condition = condition;
    this->numPages = numPages;
    this->BlockLoaded = false;
    this->invalid = false;
    this->blockPtr = 0;
    for (int i = 0; i < this->leftAttrs.size(); i++) {
        excludeTableName(this->leftAttrs[i].name);
    }
    for (int i = 0; i < this->rightAttrs.size(); i++) {
        excludeTableName(this->rightAttrs[i].name);
    }
}

BNLJoin::~BNLJoin() {
    free(block);
}

void BNLJoin::getAttributes(std::vector<Attribute> &attrs) const {
    for (int i = 0; i < this->leftAttrs.size(); i++) {
        attrs.push_back(leftAttrs[i]);
    }
    for (int i = 0; i < this->rightAttrs.size(); i++) {
        attrs.push_back(this->rightAttrs[i]);
    }
}

int BNLJoin::getLeftTupleSize(void *page) {
    int nullIndicatorSize = ceil((double) leftAttrs.size() / CHAR_BIT);
    int offset = 0;
    char *nullIndicator = (char *) malloc(nullIndicatorSize);
    memcpy(nullIndicator, (char *) page + offset, nullIndicatorSize);
    offset += nullIndicatorSize;
    for (int i = 0; i < leftAttrs.size(); i++) {
        bool isNull = nullIndicator[i / 8] & (1 << (7 - i % 8));
        if (isNull) {
            continue;
        }
        if (leftAttrs[i].type == TypeInt || leftAttrs[i].type == TypeReal) {
            offset += sizeof(int);
        } else {
            int length;
            memcpy(&length, (char *) page + offset, sizeof(int));
            offset + sizeof(int) + length;
        }
    }
    return offset;
}

RC BNLJoin::getNextBlock() {
    BlockLoaded = true;
    blockPtr = 0;
    int size = 0;
    int blockOffset = 0;
    this->tupleOffsets.clear();
    block = malloc(numPages * PAGE_SIZE);
    count = 0;
    int initialPos = 0;
    tupleOffsets.push_back(initialPos);
    while (leftIn->getNextTuple((char *) block + blockOffset) != RM_EOF) {
        int length = getLeftTupleSize((char *) block + blockOffset);
        size += length;
        if (size > numPages * PAGE_SIZE) {
            // Not EOF, but full
            return 1;
        }
        tupleOffsets.push_back(size);
        blockOffset += length;
    }
    if (size == 0) {
        return QE_EOF;
    }

    return 0;
}

RC BNLJoin::getNextTuple(void *data) {
    if (!BlockLoaded) {
        leftTuple = malloc(PAGE_SIZE);
        rightTuple = malloc(PAGE_SIZE);
        if (this->getNextBlock() == QE_EOF || rightIn->getNextTuple(rightTuple) == QE_EOF) {
            free(leftTuple);
            free(rightTuple);
            return QE_EOF;
        }
    }
    if (this->tupleOffsets.size() == 0) {
        free(leftTuple);
        free(rightTuple);
        return QE_EOF;
    }

    do {
        if (count == tupleOffsets.size() - 1) {
            // Get next rightTuple
            if (rightIn->getNextTuple(rightTuple) == QE_EOF) {
                // Load next block
                if (this->getNextBlock() == QE_EOF) {
                    return QE_EOF;
                } else {
                    // Iterate from begin of right table
                    rightIn->reset();
                    rightIn->getNextTuple(rightTuple);
                }
            } else {
                count = 0;
            }
        }
        memcpy(leftTuple, (char *) block + tupleOffsets[count], tupleOffsets[count + 1] - tupleOffsets[count]);
        count++;
    } while (!isEqual());

    if (invalid) {
        invalid = false;
        free(leftTuple);
        free(rightTuple);
        return -1;
    }
    integrateJoinResult(leftTuple, rightTuple, data, leftAttrs, rightAttrs);
    return 0;
}

bool BNLJoin::isEqual() {
    void *valueL = malloc(PAGE_SIZE);
    int indexL = getAttrValue(leftAttrs, condition.lhsAttr, valueL, leftTuple);
    void *valueR = malloc(PAGE_SIZE);
    int indexR = getAttrValue(rightAttrs, condition.rhsAttr, valueR, rightTuple);
    if (indexL == -1 || indexR == -1) {
        // when compare is invalid, return true to break the loop
        invalid = true;
        free(valueL);
        free(valueR);
        return invalid;
    }
    if (compareAttr(leftAttrs[indexL], rightAttrs[indexR])) {
        bool res = compareEqual(leftAttrs[indexL], valueL, valueR);
        free(valueL);
        free(valueR);
        return res;
    }
    free(valueL);
    free(valueR);
    return false;
}

INLJoin::INLJoin(Iterator *leftIn, IndexScan *rightIn, const Condition &condition) {
    this->leftIn = leftIn;
    this->rightIn = rightIn;
    this->condition = condition;
    leftIn->getAttributes(this->leftAttrs);
    rightIn->getAttributes(this->rightAttrs);
    this->leftLoaded = false;
    for (int i = 0; i < this->leftAttrs.size(); i++) {
        excludeTableName(this->leftAttrs[i].name);
    }
    for (int i = 0; i < this->rightAttrs.size(); i++) {
        excludeTableName(this->rightAttrs[i].name);
    }
}

INLJoin::~INLJoin() {
}

void INLJoin::getAttributes(std::vector<Attribute> &attrs) const {
    for (int i = 0; i < this->leftAttrs.size(); i++) {
        attrs.push_back(this->leftAttrs[i]);
    }
    for (int i = 0; i < this->rightAttrs.size(); i++) {
        attrs.push_back(this->rightAttrs[i]);
    }
}

RC INLJoin::getNextTuple(void *data) {
    while (true) {
        if (!leftLoaded) {
            leftLoaded = true;
            leftTuple = malloc(PAGE_SIZE);
            leftKey = malloc(PAGE_SIZE);
            if (this->leftIn->getNextTuple(leftTuple) != QE_EOF) {
                int pos = getAttrValue(this->leftAttrs, this->condition.lhsAttr, leftKey, leftTuple);
                if (pos == -1) {
                    free(leftTuple);
                    free(leftKey);
                    return -1;
                }
                this->rightIn->setIterator(leftKey, leftKey, true, true);
            }
        }
        rightTuple = malloc(PAGE_SIZE);
        if (this->rightIn->getNextTuple(rightTuple) != QE_EOF) {
            integrateJoinResult(leftTuple, rightTuple, data, leftAttrs, rightAttrs);
            free(rightTuple);
            return 0;
        }
        if (this->leftIn->getNextTuple(leftTuple) != QE_EOF) {
            int pos = getAttrValue(leftAttrs, this->condition.lhsAttr, leftKey, leftTuple);
            if (pos == -1) {
                free(leftTuple);
                free(leftKey);
                return -1;
            }
            this->rightIn->setIterator(leftKey, leftKey, true, true);
            continue;
        }
        free(rightTuple);
        return QE_EOF;
    }
}

bool compareAttr(Attribute &attrL, Attribute &attrR) {
    if (attrL.type == attrR.type && attrL.name == attrR.name && attrL.length == attrR.length) {
        return true;
    }
    return false;
}

void integrateJoinResult(void *leftTuple, void *rightTuple, void *integratedResuelt, vector<Attribute> &leftAttrs,
                         vector<Attribute> &rightAttrs) {
    int nullIndicatorSize = ceil((double) (leftAttrs.size() + rightAttrs.size()) / CHAR_BIT);
    char *nullIndicator = (char *) malloc(nullIndicatorSize);
    memset(nullIndicator, 0, nullIndicatorSize);
    int nullIndicatorLSize = ceil((double) leftAttrs.size() / CHAR_BIT);
    char *nullIndicatorL = (char *) malloc(nullIndicatorLSize);
    memcpy(nullIndicatorL, leftTuple, nullIndicatorLSize);
    int nullIndicatorRSize = ceil((double) rightAttrs.size() / CHAR_BIT);
    char *nullIndicatorR = (char *) malloc(nullIndicatorRSize);
    memcpy(nullIndicatorR, rightTuple, nullIndicatorRSize);
    int offset = nullIndicatorSize;
    int offsetL = nullIndicatorLSize;
    int offsetR = nullIndicatorRSize;
    for (int i = 0; i < leftAttrs.size() + rightAttrs.size(); i++) {
        if (i < leftAttrs.size()) {
            bool isNull = nullIndicatorL[i / 8] & (1 << (7 - i % 8));
            if (isNull) {
                nullIndicator[i / 8] |= 1 << (7 - i % 8);
                continue;
            }
            FillAttrValue(integratedResuelt, leftTuple, offset, offsetL, leftAttrs[i]);
        } else {
            int index = i - leftAttrs.size();
            bool isNull = nullIndicatorR[index / 8] & (1 << (7 - index % 8));
            if (isNull) {
                nullIndicator[i / 8] |= 1 << (7 - i % 8);
                continue;
            }
            FillAttrValue(integratedResuelt, rightTuple, offset, offsetR, rightAttrs[index]);
        }
    }
    memcpy(integratedResuelt, nullIndicator, nullIndicatorSize);
    free(nullIndicator);
    free(nullIndicatorL);
    free(nullIndicatorR);
}

void FillAttrValue(void *des, void *srs, int &desOffset, int &srsOffset, Attribute &attr) {
    if (attr.type == TypeInt || attr.type == TypeReal) {
        memcpy((char *) des + desOffset, (char *) srs + srsOffset, sizeof(int));
        desOffset += sizeof(int);
        srsOffset += sizeof(int);
    } else {
        int length;
        memcpy(&length, (char *) srs + srsOffset, sizeof(int));
        memcpy((char *) des + desOffset, &length, sizeof(int));
        srsOffset += sizeof(int);
        desOffset += sizeof(int);
        memcpy((char *) des + desOffset, (char *) srs + srsOffset, length);
        srsOffset += length;
        desOffset += length;
    }
}

void excludeTableName(string &name) {
    int pos = name.find('.');
    string attrName = name.substr(pos + 1, name.length() - pos + 1);
    name = attrName;
}

bool compareEqual(Attribute &attr, const void *compValue, const void *compKey) {
    bool equal;
    switch (attr.type) {
        case TypeInt:
            int valueI;
            int keyI;
            memcpy(&valueI, compValue, sizeof(int));
            memcpy(&keyI, compKey, sizeof(int));
            equal = valueI == keyI;
            break;
        case TypeReal:
            float valueII;
            float keyII;
            memcpy(&valueII, compValue, sizeof(int));
            memcpy(&keyII, compKey, sizeof(int));
            equal = valueII == keyII;
            break;
        case TypeVarChar:
            string valueStr;
            string keyStr;
            int valueLen;
            int keyLen;
            memcpy(&valueLen, (char *) compValue, sizeof(int));
            memcpy(&keyLen, (char *) compKey, sizeof(int));
            char *value = (char *) malloc(valueLen + 1);
            char *key = (char *) malloc(keyLen + 1);
            memcpy(value, (char *) compValue + sizeof(int), valueLen);
            memcpy(key, (char *) compKey + sizeof(int), keyLen);
            value[valueLen] = '\0';
            key[keyLen] = '\0';
            valueStr = string(value);
            keyStr = string(key);
            equal = valueStr == keyStr;
            free(value);
            free(key);
            break;
    }
    return equal;
}

int compareLess(Attribute &attr, const void *compValue, const void *compKey) {
    if (attr.type == TypeInt) {
        int value;
        int key;
        memcpy(&value, compValue, sizeof(int));
        memcpy(&key, compKey, sizeof(int));
        if (value < key) {
            return 1;
        } else if (value == key) {
            return 0;
        }
    } else if (attr.type == TypeReal) {
        float value;
        float key;
        memcpy(&value, compValue, sizeof(int));
        memcpy(&key, compKey, sizeof(int));
        if (value < key) {
            return 1;
        } else if (value == key) {
            return 0;
        }
    } else if (attr.type == TypeVarChar) {
        string valueStr;
        string keyStr;
        int valueLen;
        int keyLen;
        memcpy(&valueLen, (char *) compValue, sizeof(int));
        memcpy(&keyLen, (char *) compKey, sizeof(int));
        char *value = (char *) malloc(valueLen + 1);
        char *key = (char *) malloc(keyLen + 1);
        memcpy(value, (char *) compValue + sizeof(int), valueLen);
        memcpy(key, (char *) compKey + sizeof(int), keyLen);
        value[valueLen] = '\0';
        key[keyLen] = '\0';
        valueStr = string(value);
        keyStr = string(key);
        if (valueStr < keyStr) {
            free(value);
            free(key);
            return 1;
        } else if (valueStr == keyStr) {
            free(value);
            free(key);
            return 0;
        }
        free(value);
        free(key);
    }
    return -1;
}

int compareLarge(Attribute &attr, const void *compValue, const void *compKey) {
    if (attr.type == TypeInt) {
        int value;
        int key;
        memcpy(&value, compValue, sizeof(int));
        memcpy(&key, compKey, sizeof(int));
        if (value > key) {
            return 1;
        } else if (value == key) {
            return 0;
        }
    } else if (attr.type == TypeReal) {
        float value;
        float key;
        memcpy(&value, compValue, sizeof(int));
        memcpy(&key, compKey, sizeof(int));
        if (value > key) {
            return 1;
        } else if (value == key) {
            return 0;
        }
    } else if (attr.type == TypeVarChar) {
        string valueStr;
        string keyStr;
        int valueLen;
        int keyLen;
        memcpy(&valueLen, (char *) compValue, sizeof(int));
        memcpy(&keyLen, (char *) compKey, sizeof(int));
        char *value = (char *) malloc(valueLen + 1);
        char *key = (char *) malloc(keyLen + 1);
        memcpy(value, (char *) compValue + sizeof(int), valueLen);
        memcpy(key, (char *) compKey + sizeof(int), keyLen);
        value[valueLen] = '\0';
        key[keyLen] = '\0';
        valueStr = string(value);
        keyStr = string(key);
        if (valueStr > keyStr) {
            free(value);
            free(key);
            return 1;
        } else if (valueStr == keyStr) {
            free(value);
            free(key);
            return 0;
        }
        free(value);
        free(key);
    }
    return -1;
}

GroupAttr::GroupAttr() {
    this->sum = 0;
    this->count = 0;
    this->max = numeric_limits<float>::min();
    this->min = numeric_limits<float>::max();
}

Aggregate::Aggregate(Iterator *input, Attribute aggAttr, AggregateOp op) {
    this->input = input;
    this->groupOpFlag = false;
    input->getAttributes(this->attrs);
    this->aop = op;
    this->aggAttr = aggAttr;
    this->mapSize = 1;

    for (int i = 0; i < this->attrs.size(); ++i) {
        int pos = attrs[i].name.find('.');
        string attrName = attrs[i].name.substr(pos + 1, attrs[i].name.length() - pos + 1);
        this->rel = attrs[i].name.substr(0, pos);
        this->attrs[i].name = attrName;
    }
    int pos = aggAttr.name.find('.');
    this->aggAttr.name = aggAttr.name.substr(pos + 1, aggAttr.name.length() - pos + 1);

    this->buildAggResult();
}

Aggregate::Aggregate(Iterator *input, Attribute aggAttr, Attribute groupAttr, AggregateOp op) {
    this->input = input;
    this->groupOpFlag = true;
    input->getAttributes(this->attrs);
    this->aop = op;
    this->aggAttr = aggAttr;
    this->mapSize = 1;

    for (int i = 0; i < this->attrs.size(); ++i) {
        int pos = attrs[i].name.find('.');
        string attrName = attrs[i].name.substr(pos + 1, attrs[i].name.length() - pos + 1);
        this->rel = attrs[i].name.substr(0, pos);
        this->attrs[i].name = attrName;
    }
    int pos = aggAttr.name.find('.');
    this->aggAttr.name = aggAttr.name.substr(pos + 1, aggAttr.name.length() - pos + 1);

    this->groupAttr = groupAttr;
    pos = groupAttr.name.find('.');
    this->groupAttr.name = groupAttr.name.substr(pos + 1, groupAttr.name.length() - pos + 1);

    this->buildAggResult();
}

void Aggregate::buildAggResult() {
    void *data = malloc(PAGE_SIZE);
    memset(data, 0, PAGE_SIZE);
    while (this->input->getNextTuple(data) != RM_EOF) {
        void *value = malloc(PAGE_SIZE);
        int rc = this->getAttrValueByName(data, this->attrs, this->aggAttr.name, value);
        if (rc != 0) {
            free(value);
            free(data);
            return;
        }
        float numeric = 0;
        switch (this->aggAttr.type) {
            case TypeInt: {
                int itg;
                memcpy(&itg, value, sizeof(int));
                numeric = (float) itg;
                break;
            }
            case TypeReal: {
                float flt;
                memcpy(&flt, value, sizeof(float));
                numeric = flt;
                break;
            }
            case TypeVarChar: {
                numeric = 0;
                break;
            }
        }
        if (this->groupOpFlag) {
            void *groupVal = malloc(PAGE_SIZE);
            memset(groupVal, 0, PAGE_SIZE);
            rc = this->getAttrValueByName(data, this->attrs, this->groupAttr.name, groupVal);
            switch (this->groupAttr.type) {
                case TypeInt: {
                    int itg;
                    memcpy(&itg, groupVal, sizeof(int));

                    auto itgItr = this->itgMap.find(itg);
                    if (itgItr == this->itgMap.end()) {
                        GroupAttr gAttr;
                        gAttr.sum += numeric;
                        gAttr.count++;
                        this->itgMap[itg] = gAttr;
                    } else {
                        itgItr->second.sum += numeric;
                        itgItr->second.count++;
                        itgItr->second.max = numeric > (itgItr->second.max) ? numeric : itgItr->second.max;
                        itgItr->second.min = numeric < (itgItr->second.min) ? numeric : itgItr->second.min;
                    }
                    this->itgIter = this->itgMap.begin();
                    this->mapSize = this->itgMap.size();
                    break;
                }
                case TypeReal: {
                    int flt;
                    memcpy(&flt, groupVal, sizeof(float));

                    auto fltItr = this->fltMap.find(flt);
                    if (fltItr == this->fltMap.end()) {
                        GroupAttr gAttr;
                        gAttr.sum += numeric;
                        gAttr.count++;
                        this->fltMap[flt] = gAttr;
                    } else {
                        fltItr->second.sum += numeric;
                        fltItr->second.count++;
                        fltItr->second.max = numeric > (fltItr->second.max) ?
                                             numeric : fltItr->second.max;
                        fltItr->second.min = numeric < (fltItr->second.min) ?
                                             numeric : fltItr->second.min;
                    }
                    this->fltIter = this->fltMap.begin();
                    this->mapSize = this->fltMap.size();
                    break;
                }
                case TypeVarChar: {
                    int len;
                    memcpy(&len, groupVal, sizeof(int));
                    void *str = malloc(len);
                    memset(str, 0, len);
                    memcpy(str, (char *) groupVal + sizeof(int), len);
                    string vchar = string((char *) str, len);
                    free(str);
                    auto vcharItr = this->vcharMap.find(vchar);
                    if (vcharItr == this->vcharMap.end()) {
                        GroupAttr gAttr;
                        gAttr.sum += numeric;
                        gAttr.count++;
                        this->vcharMap[vchar] = gAttr;
                    } else {
                        vcharItr->second.sum += numeric;
                        vcharItr->second.count++;
                        vcharItr->second.min = numeric;
                        vcharItr->second.max = numeric;
                    }
                    this->vcharIter = this->vcharMap.begin();
                    this->mapSize = this->vcharMap.size();
                    break;
                }
            }
            free(groupVal);
        } else {
            this->gpAttr.count++;
            this->gpAttr.sum += numeric;
            this->gpAttr.max = numeric > this->gpAttr.max ?
                               numeric : this->gpAttr.max;
            this->gpAttr.min = numeric < this->gpAttr.min ?
                               numeric : this->gpAttr.min;
        }
        free(value);
    }
    free(data);
}

RC Aggregate::getAttrValueByName(const void *data, vector<Attribute> &attrs, string &attrName, void *value) {
    int pos;
    for (pos = 0; pos < attrs.size(); pos++) {
        if (attrs[pos].name == attrName) {
            break;
        }
    }
    if (pos == attrs.size()) {
        return -1;
    }

    int fieldCount = attrs.size();
    int nullFlagSize = ceil((double) fieldCount / CHAR_BIT);
    unsigned char *nullFlags = (unsigned char *) malloc(nullFlagSize);
    memset(nullFlags, 0, nullFlagSize);
    memcpy((char *) nullFlags, (char *) data, nullFlagSize);

    int offset = nullFlagSize;
    int nByte = pos / 8;
    int nBit = pos % 8;
    bool nullBit = nullFlags[nByte] & (1 << (7 - nBit));

    if (nullBit) {
        return -1;
    }

    for (int i = 0; i < fieldCount; i++) {
        nByte = i / 8;
        nBit = i % 8;
        nullBit = nullFlags[nByte] & (1 << (7 - nBit));
        if (nullBit) {
            continue;
        }

        if (attrs[i].type == TypeVarChar) {
            int len;
            memcpy(&len, (char *) data + offset, sizeof(int));
            offset += sizeof(int);
            memcpy(value, &len, sizeof(int));
            memcpy((char *) value + sizeof(int), (char *) data + offset, len);
            offset += len;
        } else if (attrs[i].type == TypeInt) {
            memcpy(value, (char *) data + offset, sizeof(int));
            offset += attrs[i].length;
        } else if (attrs[i].type == TypeReal) {
            memcpy(value, (char *) data + offset, sizeof(float));
            offset += attrs[i].length;
        }
        if (i == pos) {
            break;
        }
    }
    free(nullFlags);
    return 0;
}

RC Aggregate::getNextTuple(void *data) {
    if (this->current >= this->mapSize) {
        return QE_EOF;
    }

    int fieldCount = 1;
    int nullFlagSize = ceil((double) fieldCount / CHAR_BIT);
    unsigned char *nullFlags = (unsigned char *) malloc(nullFlagSize);
    memset(nullFlags, 0, nullFlagSize);
    int offset = nullFlagSize;
    float numeric;
    if (this->groupOpFlag) {
        GroupAttr gAttr;
        switch (this->groupAttr.type) {
            case TypeInt: {
                memcpy((char *) data + offset, &this->itgIter->first, sizeof(int));
                offset += sizeof(int);
                gAttr = this->itgIter->second;
                this->itgIter++;
                break;
            }
            case TypeReal: {
                memcpy((char *) data + offset, &this->fltIter->first, sizeof(float));
                offset += sizeof(float);
                gAttr = this->fltIter->second;
                this->fltIter++;
                break;
            }
            case TypeVarChar: {
                int len = this->vcharIter->first.length();
                memcpy((char *) data + offset, &len, sizeof(int));
                offset += sizeof(int);
                memcpy((char *) data + offset, &this->vcharIter->first, len);
                gAttr = this->vcharIter->second;
                this->vcharIter++;
                break;
            }
        }
        this->gpAttr = gAttr;
    }
    switch (this->aop) {
        case MIN:
            numeric = this->gpAttr.min;
            break;
        case MAX:
            numeric = this->gpAttr.max;
            break;
        case COUNT:
            numeric = this->gpAttr.count;
            break;
        case AVG:
            numeric = this->gpAttr.sum / this->gpAttr.count;
            break;
        case SUM:
            numeric = this->gpAttr.sum;
            break;
    }
    memcpy((char *) data + offset, &numeric, sizeof(float));
    this->current++;

    memcpy(data, nullFlags, nullFlagSize);
    free(nullFlags);
    return 0;
}

void Aggregate::getAttributes(vector<Attribute> &attrs) const {
    attrs.clear();

    if (this->groupOpFlag) {
        string fullName = this->rel + "." + this->groupAttr.name;
        Attribute attr = this->groupAttr;
        attr.name = fullName;
        attrs.push_back(attr);
    }

    //Put aggregate op name;
    string printName = getOpName();
    printName += "(";
    printName += this->rel + "." + this->aggAttr.name;
    printName += ")";
    Attribute attr = this->aggAttr;
    attr.name = printName;
    attrs.push_back(attr);
}

string Aggregate::getOpName() const {
    switch (this->aop) {
        case MIN:
            return "MIN";
            break;
        case MAX:
            return "MAX";
            break;
        case COUNT:
            return "COUNT";
            break;
        case AVG:
            return "AVG";
            break;
        case SUM:
            return "SUM";
            break;
    }
}
