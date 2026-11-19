from . import CosimEnums


class CosimSlaveInfo:
    """Name and index of a slave object"""

    def __init__(self, name: bytes = b"", index: int = 0):
        self.name = name
        self.index = index

    def __repr__(self):
        return "name: {0}, index: {1}".format(self.name.decode(), self.index)


class CosimSlaveVariableDescription:
    """Slave variable information object"""

    def __init__(self, name: bytes, reference: int, type: int, causality: int, variability: int):
        self.name = name
        self.reference = reference
        self.type = type
        self.causality = causality
        self.variability = variability

    def __repr__(self):
        return "name: {0}, reference: {1}, type: {2}, causality: {3}, variability: {4}".format(
            self.name,
            self.reference,
            CosimEnums.CosimVariableType(self.type),
            CosimEnums.CosimVariableCausality(self.causality),
            CosimEnums.CosimVariableVariability(self.variability),
        )


class CosimLocalSlave:
    """Locally created execution slave (an FMU to be added to an execution)"""

    def __init__(self, fmu_path: str, instance_name: str):
        self.fmu_path = fmu_path
        self.instance_name = instance_name

    def ptr(self):
        return self
