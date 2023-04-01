Imports System.Collections.ObjectModel
Imports System.ComponentModel
Imports Windows.Devices.Bluetooth
Imports Windows.Devices.Bluetooth.Advertisement

Class MainWindow
	Implements INotifyPropertyChanged

	Public Event PropertyChanged As PropertyChangedEventHandler Implements INotifyPropertyChanged.PropertyChanged

	Private WithEvents BleWatcher As BluetoothLEAdvertisementWatcher
	Public ReadOnly Property ScannedDevices As New ObservableCollection(Of StopwatchDevice)

	Public Property SelectedDevice As StopwatchDevice
		Get
			Return _selectedDevice
		End Get
		Set(value As StopwatchDevice)
			_selectedDevice = value
			RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(SelectedDevice)))
		End Set
	End Property
	Private ReadOnly Property KnownDevices As New Dictionary(Of ULong, StopwatchDevice)

	Private _selectedDevice As StopwatchDevice

	Public Sub New()
		Me.DataContext = Me
		InitializeComponent()
		BleWatcher = New BluetoothLEAdvertisementWatcher()
		BleWatcher.ScanningMode = BluetoothLEScanningMode.Active
		BleWatcher.Start()

		BindingOperations.EnableCollectionSynchronization(ScannedDevices, ScannedDevices)
	End Sub

	Private Sub BleWatcher_Received(sender As BluetoothLEAdvertisementWatcher, args As BluetoothLEAdvertisementReceivedEventArgs) Handles BleWatcher.Received
		If args.Advertisement.LocalName = "Misaz test" Then
			Dim bleAddr = args.BluetoothAddress

			If Not KnownDevices.ContainsKey(bleAddr) Then
				AddNewDevice(bleAddr, args.Advertisement.LocalName)
			Else
				RefreshDeviceVisibility(bleAddr)
			End If
		End If
	End Sub

	Private Sub RefreshDeviceVisibility(bleAddr As ULong)
		KnownDevices(bleAddr).RefreshVisiblity()
	End Sub

	Private Sub AddNewDevice(bleAddr As ULong, name As String)
		Dim myDev = New StopwatchDevice(bleAddr, name)

		KnownDevices.Add(bleAddr, myDev)
		ScannedDevices.Add(myDev)
	End Sub


	Private Async Sub Connect_Click(sender As Object, e As RoutedEventArgs)
		Dim dev As StopwatchDevice = CType(sender, Button).Tag
		Try
			Await dev.ConnectAsync()
		Catch ex As Exception
			Dispatcher.Invoke(
				Sub()
					MessageBox.Show("Connection failed. Details: " & vbCrLf & vbCrLf & ex.GetType().Name & ": " & ex.Message, "Connection failed", MessageBoxButton.OK, MessageBoxImage.Error)
				End Sub)
		End Try
	End Sub
End Class
