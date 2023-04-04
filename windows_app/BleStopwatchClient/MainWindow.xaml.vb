Imports System.Collections.ObjectModel
Imports System.ComponentModel
Imports System.IO
Imports Microsoft.Win32
Imports Windows.Devices.Bluetooth
Imports Windows.Devices.Bluetooth.Advertisement

Class MainWindow
	Implements INotifyPropertyChanged

	Public Event PropertyChanged As PropertyChangedEventHandler Implements INotifyPropertyChanged.PropertyChanged

	Private WithEvents BleWatcher As New BluetoothLEAdvertisementWatcher()
	Public ReadOnly Property ScannedDevices As New ObservableCollection(Of StopwatchDevice)

	Public Property SelectedDevice As StopwatchDevice
		Get
			Return _selectedDevice
		End Get
		Set(value As StopwatchDevice)
			_selectedDevice = value
			RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(SelectedDevice)))
			RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(DeviceDetailsVisibility)))
		End Set
	End Property
	Private ReadOnly Property KnownDevices As New Dictionary(Of ULong, StopwatchDevice)

	Private _selectedDevice As StopwatchDevice

	Public ReadOnly Property DeviceDetailsVisibility As Visibility
		Get
			If SelectedDevice IsNot Nothing Then
				Return Visibility.Visible
			Else
				Return Visibility.Hidden
			End If
		End Get
	End Property

	Public ReadOnly Property ScanStatus As String
		Get
			If BleWatcher.Status = BluetoothLEAdvertisementWatcherStatus.Started Or BleWatcher.Status = BluetoothLEAdvertisementWatcherStatus.Created Then
				Return "In progress"
			Else
				Return "Completed"
			End If
		End Get
	End Property

	Public ReadOnly Property RescanVisibility As Visibility
		Get
			If BleWatcher.Status = BluetoothLEAdvertisementWatcherStatus.Started Or BleWatcher.Status = BluetoothLEAdvertisementWatcherStatus.Created Then
				Return Visibility.Hidden
			Else
				Return Visibility.Visible
			End If
		End Get
	End Property

	Public Sub New()
		Me.DataContext = Me
		InitializeComponent()
		BleWatcher.ScanningMode = BluetoothLEScanningMode.Active
		BleWatcher.Start()
		RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ScanStatus)))
		RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(RescanVisibility)))

		BindingOperations.EnableCollectionSynchronization(ScannedDevices, ScannedDevices)
	End Sub

	Private Sub BleWatcher_Received(sender As BluetoothLEAdvertisementWatcher, args As BluetoothLEAdvertisementReceivedEventArgs) Handles BleWatcher.Received
		If args.Advertisement.LocalName = "Misaz Stopwatch" Then
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
		Dim dev As StopwatchDevice = SelectedDevice
		Try
			Await dev.ConnectAsync()
		Catch ex As Exception
			Dispatcher.Invoke(
				Sub()
					MessageBox.Show("Connection failed. Details: " & vbCrLf & vbCrLf & ex.GetType().Name & ": " & ex.Message, "Connection failed", MessageBoxButton.OK, MessageBoxImage.Error)
				End Sub)
		End Try
	End Sub

	Private Sub ExportLaps_Click(sender As Object, e As RoutedEventArgs)
		Dim sfd As New SaveFileDialog()
		sfd.Filter = "CSV File (*.csv)|*.csv|All files|*"
		If sfd.ShowDialog() Then
			Try
				Using sw As New StreamWriter(sfd.FileName)
					For Each l In SelectedDevice.Laps
						sw.WriteLine($"{l.Number},{l.Time},""{l.Description.Replace("""", "").Replace("\", "")}""")
					Next
				End Using
			Catch ex As Exception
				MessageBox.Show("Error while writing file. Details: " & vbCrLf & vbCrLf & ex.GetType().Name & ": " & ex.Message, "Write failed", MessageBoxButton.OK, MessageBoxImage.Error)
			End Try
		End If

	End Sub

	Private Sub BleWatcher_Stopped(sender As BluetoothLEAdvertisementWatcher, args As BluetoothLEAdvertisementWatcherStoppedEventArgs) Handles BleWatcher.Stopped
		RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ScanStatus)))
		RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(RescanVisibility)))
	End Sub

	Private Sub Rescan_Click(sender As Object, e As RoutedEventArgs)
		BleWatcher.Start()
		RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(ScanStatus)))
		RaiseEvent PropertyChanged(Me, New PropertyChangedEventArgs(NameOf(RescanVisibility)))
	End Sub
End Class
