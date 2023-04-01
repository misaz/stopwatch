Imports System.ComponentModel
Imports System.Runtime.InteropServices.WindowsRuntime
Imports Windows.Devices.Bluetooth
Imports Windows.Devices.Bluetooth.GenericAttributeProfile
Imports Windows.Gaming.Input.Custom

Public Class StopwatchDevice
    Implements INotifyPropertyChanged

    Public Event PropertyChanged As PropertyChangedEventHandler Implements INotifyPropertyChanged.PropertyChanged

    Private ReadOnly StopwatchServiceGuid As Guid = Guid.Parse("2C611E88-85CC-7C21-D6F5-9595051FCA00")
    Private ReadOnly StatusCharacteristicsGuid As Guid = Guid.Parse("2C611E88-85CC-7C21-D6F5-9595051FCA01")

    Public ReadOnly Property Address As String
        Get
            Return String.Join(":", From x In BitConverter.GetBytes(_bleAddress) Select x.ToString("X2"))
        End Get
    End Property

    Public ReadOnly Property Status As String
        Get
            If _isConnected Then
                If _isStopwatchRunnig Then
                    Return "Running"
                Else
                    Return "Ready"
                End If
            Else
                If _isConnecting Then
                    Return "Connecting"
                Else
                    Return "Not Connected"
                End If
            End If
        End Get
    End Property

    Public ReadOnly Property ConnectButtonVisibility As Visibility
        Get
            If _isConnected Then
                Return Visibility.Hidden
            Else
                Return Visibility.Visible
            End If
        End Get
    End Property

    Private _bleDevice As BluetoothLEDevice
    Private _bleAddress As ULong
    Private _isConnected = False
    Private _isConnecting = False
    Private _isStopwatchRunnig As Boolean = False
    Public Sub New(bleAddress As ULong)
        _bleAddress = bleAddress
    End Sub


    Friend Sub RefreshVisiblity()
        Throw New NotImplementedException()
    End Sub

    Public Async Function ConnectAsync() As Task
        _isConnecting = True
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))

        Try
            _bleDevice = Await BluetoothLEDevice.FromBluetoothAddressAsync(_bleAddress)
        Catch ex As Exception
            Debug.WriteLine($"Error while creating BluetoothLEDevice with BLE address {_bleAddress}")
            _isConnecting = False
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
            Return
        End Try

        AddHandler _bleDevice.ConnectionStatusChanged, AddressOf ConnectionStatusChanged

        Dim servicesSearch As GattDeviceServicesResult
        Try
            servicesSearch = Await _bleDevice.GetGattServicesForUuidAsync(StopwatchServiceGuid)
        Catch ex As Exception
            Debug.WriteLine($"Error while enumerating GATT serivies.")
            _isConnecting = False
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
            Return
        End Try
        If servicesSearch.Services.Count <> 1 Then
            Debug.WriteLine($"Unexpected number of services found ({servicesSearch.Services.Count}).")
            _isConnecting = False
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
            Return
        End If

        Dim stopwatchService = servicesSearch.Services(0)

        Dim statusCharacteristics = Await GetCharacteristics(stopwatchService, StatusCharacteristicsGuid)
        Dim statusReadResult = Await statusCharacteristics.ReadValueAsync()
        If statusReadResult.Status <> GattCommunicationStatus.Success Then
            Debug.WriteLine($"Reading status characteristics failed with status code ({statusReadResult.Status}).")
            _isConnecting = False
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
            Return
        End If
        If statusReadResult.Value.Length <> 1 Then
            Debug.WriteLine($"Reading status characteristics failed because invalid value was received.")
            _isConnecting = False
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
            Return
        End If

        Dim statusVal(1) As Byte
        statusReadResult.Value.CopyTo(statusVal)
        SetServiceStatus(statusVal(0))

        AddHandler statusCharacteristics.ValueChanged, AddressOf StatusValueChanged

        Dim cccChangeStatus = Await statusCharacteristics.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.Notify)
        If cccChangeStatus <> GattCommunicationStatus.Success Then
            Debug.WriteLine($"Writing Status CCC failed with status code ({cccChangeStatus}).")
            _isConnecting = False
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
            Return
        End If

        _isConnecting = False
        _isConnected = True
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
    End Function

    Private Sub ConnectionStatusChanged(sender As BluetoothLEDevice, args As Object)
        _isConnected = _bleDevice.ConnectionStatus = BluetoothConnectionStatus.Connected
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
    End Sub

    Private Sub StatusValueChanged(sender As GattCharacteristic, args As GattValueChangedEventArgs)
        If args.CharacteristicValue.Length <> 1 Then
            Debug.WriteLine("Received status change notification with invalid value.")
            Return
        End If

        Dim val(1) As Byte
        args.CharacteristicValue.CopyTo(val)

        SetServiceStatus(val(0))
    End Sub

    Private Sub SetServiceStatus(v As Byte)
        _isStopwatchRunnig = v = 1

        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ConnectButtonVisibility)))
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
    End Sub

    Private Async Function GetCharacteristics(service As GattDeviceService, characteristicsGuid As Guid) As Task(Of GattCharacteristic)
        Dim searchResult As GattCharacteristicsResult
        Try
            searchResult = Await service.GetCharacteristicsForUuidAsync(StatusCharacteristicsGuid)
        Catch ex As Exception
            Debug.WriteLine($"Error while enumerating GATT characteristics with GUID {StatusCharacteristicsGuid}.")
            Return Nothing
        End Try

        If searchResult.Characteristics.Count <> 1 Then
            Debug.WriteLine($"Unexpected number of characteristics found ({searchResult.Characteristics.Count}) for UUID {characteristicsGuid}.")
            Return Nothing
        End If

        Return searchResult.Characteristics(0)
    End Function


End Class
