Imports System.Collections.ObjectModel
Imports System.ComponentModel
Imports System.Formats
Imports System.Runtime.InteropServices.WindowsRuntime
Imports System.Windows.Threading
Imports Windows.Devices.Bluetooth
Imports Windows.Devices.Bluetooth.GenericAttributeProfile
Imports Windows.Gaming.Input.Custom

Public Class StopwatchDevice
    Implements INotifyPropertyChanged

    Public Event PropertyChanged As PropertyChangedEventHandler Implements INotifyPropertyChanged.PropertyChanged

    Private ReadOnly StopwatchServiceGuid As Guid = Guid.Parse("2C611E88-85CC-7C21-D6F5-9595051FCA00")
    Private ReadOnly StatusCharacteristicsGuid As Guid = Guid.Parse("2C611E88-85CC-7C21-D6F5-9595051FCA01")
    Private ReadOnly ElapsedTimeCharacteristicsGuid As Guid = Guid.Parse("2C611E88-85CC-7C21-D6F5-9595051FCA10")
    Private ReadOnly LapsCountCharacteristicsGuid As Guid = Guid.Parse("2C611E88-85CC-7C21-D6F5-9595051FCA20")
    Private ReadOnly LapSelectCharacteristicsGuid As Guid = Guid.Parse("2C611E88-85CC-7C21-D6F5-9595051FCA21")
    Private ReadOnly LapTimeCharacteristicsGuid As Guid = Guid.Parse("2C611E88-85CC-7C21-D6F5-9595051FCA22")

    Public ReadOnly Property Address As String
        Get
            Return String.Join(":", From x In BitConverter.GetBytes(_bleAddress).Take(6).Reverse() Select x.ToString("X2"))
        End Get
    End Property

    Public ReadOnly Property Status As String
        Get
            If _isConnected Then
                If _isStopwatchRunnig Then
                    Return "Stopwatch running"
                Else
                    Return "Stopwatch stopped"
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

    Public ReadOnly Property CanConnect As Boolean
        Get
            Return Not _isConnected
        End Get
    End Property

    Public ReadOnly Property Laps As ObservableCollection(Of Lap)
        Get
            Return _laps
        End Get
    End Property

    Public ReadOnly Property ElapsedTime As TimeSpan
        Get
            If _isStopwatchRunnig Then
                Return _elapsedTime + (DateTime.UtcNow - _elapsedTimeSnapshot)
            Else
                Return _elapsedTime
            End If
        End Get
    End Property

    Public ReadOnly Property Name As String
        Get
            Return _bleName
        End Get
    End Property

    Private _bleDevice As BluetoothLEDevice
    Private _bleAddress As ULong
    Private _bleName As String
    Private _isConnected = False
    Private _isConnecting = False
    Private _isStopwatchRunnig As Boolean = False
    Private _statusCharacteristics As GattCharacteristic
    Private _elapsedTimeCharacteristics As GattCharacteristic
    Private _lapsCountCharacteristics As GattCharacteristic
    Private _lapSelectCharacteristics As GattCharacteristic
    Private _lapTimeCharacteristics As GattCharacteristic
    Private _loadedLaps As Integer = 0
    Private _laps As New ObservableCollection(Of Lap)
    Private _elapsedTime As TimeSpan
    Private _elapsedTimeSnapshot As DateTime
    Private _elapsedTimeUpdateTimer As New DispatcherTimer(DispatcherPriority.Background, Application.Current.Dispatcher)

    Public Sub New(bleAddress As ULong, bleName As String)
        _bleAddress = bleAddress
        _bleName = bleName
        BindingOperations.EnableCollectionSynchronization(_laps, _laps)
        _elapsedTimeUpdateTimer.Interval = New TimeSpan(0, 0, 0, 0, 100)
        AddHandler _elapsedTimeUpdateTimer.Tick, AddressOf _elapsedTimeUpdateTimer_Tick
        _elapsedTimeUpdateTimer.Start()
    End Sub

    Public Sub RefreshVisiblity()
    End Sub

    Public Async Function ConnectAsync() As Task
        If _isConnected Then
            Throw New InvalidOperationException("Device is already connected")
        End If

        _isConnecting = True
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(CanConnect)))
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))

        Try
            Await ConnectAsyncInternal()
        Catch ex As Exception
            _isConnecting = False
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(CanConnect)))
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
            Throw
        End Try

        _isConnecting = False
        _isConnected = True
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(CanConnect)))
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
    End Function

    Public Async Function ConnectAsyncInternal() As Task
        _bleDevice = Await BluetoothLEDevice.FromBluetoothAddressAsync(_bleAddress)

        AddHandler _bleDevice.ConnectionStatusChanged, AddressOf ConnectionStatusChangedHandler

        Dim stopwatchService = Await GetService(StopwatchServiceGuid)

        _statusCharacteristics = Await GetCharacteristics(stopwatchService, StatusCharacteristicsGuid)
        _elapsedTimeCharacteristics = Await GetCharacteristics(stopwatchService, ElapsedTimeCharacteristicsGuid)
        _lapsCountCharacteristics = Await GetCharacteristics(stopwatchService, LapsCountCharacteristicsGuid)
        _lapSelectCharacteristics = Await GetCharacteristics(stopwatchService, LapSelectCharacteristicsGuid)
        _lapTimeCharacteristics = Await GetCharacteristics(stopwatchService, LapTimeCharacteristicsGuid)

        Await InitialStatusLoad()
        Await LoadElapsedTime()
        Await InitialLapsLoad()

        AddHandler _statusCharacteristics.ValueChanged, AddressOf StatusValueChangedHandler
        AddHandler _lapsCountCharacteristics.ValueChanged, AddressOf LapsChangedHandler

        Await EnableNotifications(_statusCharacteristics)
        Await EnableNotifications(_lapsCountCharacteristics)
    End Function

    Private Async Function EnableNotifications(characteristics As GattCharacteristic) As Task
        Dim cccChangeStatus = Await characteristics.WriteClientCharacteristicConfigurationDescriptorAsync(GattClientCharacteristicConfigurationDescriptorValue.Notify)
        If cccChangeStatus <> GattCommunicationStatus.Success Then
            Throw New Exception($"Writing CCC failed with status code ({cccChangeStatus}).")
        End If
    End Function

    Private Async Function InitialStatusLoad() As Task
        SetStopwatchStatus(Await ReadCharacteristicsValueUint8(_statusCharacteristics))
    End Function

    Public Async Function InitialLapsLoad() As Task
        Dim lapsCount = Await ReadCharacteristicsValueUint8(_lapsCountCharacteristics)
        Await LoadLaps(lapsCount)
    End Function

    Private Async Function LoadLaps(lapsCount As Integer) As Task
        While _loadedLaps < lapsCount
            Await WriteCharacteristicsValueUint8(_lapSelectCharacteristics, _loadedLaps)
            Dim lapTime = Await ReadCharacteristicsValueUint32(_lapTimeCharacteristics)

            Debug.WriteLine($"Lap #{_loadedLaps} time: {lapTime}")

            Application.Current.Dispatcher.Invoke(
                Sub()
                    _laps.Add(New Lap(_loadedLaps + 1, ConvertTimeToTimespan(lapTime)))
                End Sub)

            _loadedLaps += 1
        End While
    End Function

    Private Sub ConnectionStatusChangedHandler(sender As BluetoothLEDevice, args As Object)
        _isConnected = _bleDevice.ConnectionStatus = BluetoothConnectionStatus.Connected


        If Not _isConnected Then
            _isStopwatchRunnig = False
        End If

        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(CanConnect)))
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
    End Sub

    Private Async Sub StatusValueChangedHandler(sender As GattCharacteristic, args As GattValueChangedEventArgs)
        If args.CharacteristicValue.Length <> 1 Then
            Debug.WriteLine("Received status change notification with invalid value.")
            Return
        End If

        Dim val(0) As Byte
        args.CharacteristicValue.CopyTo(val)
        Dim newStatus = val(0)

        If newStatus = 1 Then
            Application.Current.Dispatcher.Invoke(
                Sub()
                    _laps.Clear()
                    _loadedLaps = 0
                End Sub)
        End If

        SetStopwatchStatus(newStatus)
        Await LoadElapsedTime()
    End Sub

    Private Async Function LoadElapsedTime() As Task
        _elapsedTimeSnapshot = DateTime.UtcNow

        Dim loadedElapsedTime = Await ReadCharacteristicsValueUint32(_elapsedTimeCharacteristics)

        Debug.WriteLine($"Refreshed elapsed time to {loadedElapsedTime}")

        _elapsedTime = ConvertTimeToTimespan(loadedElapsedTime)
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ElapsedTime)))
    End Function

    Private Async Sub LapsChangedHandler(sender As GattCharacteristic, args As GattValueChangedEventArgs)
        If args.CharacteristicValue.Length <> 1 Then
            Debug.WriteLine("Received laps count change notification with invalid value.")
        End If

        Dim val(0) As Byte
        args.CharacteristicValue.CopyTo(val)

        Try
            Await LoadLaps(val(0))
        Catch ex As Exception
            Debug.WriteLine($"Error while loading laps. Details: {ex.GetType().Name}: {ex.Message}")
        End Try
    End Sub

    Private Sub SetStopwatchStatus(isRunning As Byte)
        _isStopwatchRunnig = isRunning = 1

        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(CanConnect)))
        RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(Status)))
    End Sub

    Private Async Function GetService(stopwatchServiceGuid As Guid) As Task(Of GattDeviceService)
        Dim servicesSearch As GattDeviceServicesResult = Await _bleDevice.GetGattServicesForUuidAsync(stopwatchServiceGuid)

        If servicesSearch.Services.Count <> 1 Then
            Throw New Exception($"Unexpected number of services found ({servicesSearch.Services.Count}).")
        End If

        Return servicesSearch.Services(0)
    End Function

    Private Async Function GetCharacteristics(service As GattDeviceService, characteristicsGuid As Guid) As Task(Of GattCharacteristic)
        Dim searchResult As GattCharacteristicsResult
        Try
            searchResult = Await service.GetCharacteristicsForUuidAsync(characteristicsGuid)
        Catch ex As Exception
            Throw New Exception($"Error while enumerating GATT characteristics with GUID {characteristicsGuid}.")
        End Try

        If searchResult.Characteristics.Count <> 1 Then
            Throw New Exception($"Unexpected number of characteristics found ({searchResult.Characteristics.Count}) for UUID {characteristicsGuid}.")
        End If

        Dim characteristics = searchResult.Characteristics(0)

        Return characteristics
    End Function

    Private Async Function ReadCharacteristicsValue(characteristics As GattCharacteristic, len As Integer) As Task(Of Byte())
        Dim val(len - 1) As Byte

        Dim readResult = Await characteristics.ReadValueAsync(BluetoothCacheMode.Uncached)

        If readResult.Status <> GattCommunicationStatus.Success Then
            Throw New Exception($"GATT Read failed with status code {readResult.Status}")
        End If

        If readResult.Value.Length <> len Then
            Throw New Exception($"GATT Read provided data of invalid length ({readResult.Value.Length}). Expected was {len}")
        End If

        readResult.Value.CopyTo(val)
        Return val
    End Function

    Private Async Function WriteCharacteristicsValue(characteristics As GattCharacteristic, data As Byte()) As Task
        Dim writeResult = Await characteristics.WriteValueWithResultAsync(data.AsBuffer())

        If writeResult.Status <> GattCommunicationStatus.Success Then
            Throw New Exception($"GATT Write failed with status code {writeResult.Status}")
        End If
    End Function

    Private Async Function ReadCharacteristicsValueUint8(characteristics As GattCharacteristic) As Task(Of Byte)
        Return (Await ReadCharacteristicsValue(characteristics, 1))(0)
    End Function

    Private Async Function ReadCharacteristicsValueUint32(characteristics As GattCharacteristic) As Task(Of UInteger)
        Return BitConverter.ToUInt32(Await ReadCharacteristicsValue(characteristics, 4), 0)
    End Function

    Private Async Function WriteCharacteristicsValueUint8(characteristics As GattCharacteristic, value As Byte) As Task
        Await WriteCharacteristicsValue(characteristics, New Byte() {value})
    End Function

    Private Sub _elapsedTimeUpdateTimer_Tick(sender As Object, e As EventArgs)
        If _isStopwatchRunnig Then
            RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ElapsedTime)))
        End If
    End Sub

    Private Function ConvertTimeToTimespan(time As UInteger) As TimeSpan
        Return New TimeSpan(0, 0, 0, 0, Math.Floor(time / 32.768))
    End Function
End Class
