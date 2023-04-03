Public Class Lap

    Public ReadOnly Property Number As Integer
    Public ReadOnly Property Time As TimeSpan
    Public Property Description As String = ""

    Public Sub New(number As Integer, time As TimeSpan)
        Me.Number = number
        Me.Time = time
    End Sub
End Class
